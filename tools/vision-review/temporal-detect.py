#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Detector temporal determinista de glitches (parpadeo, tearing, corrupción).

Primera capa del enfoque híbrido de verificación visual: **antes** de consultar a un modelo de
visión (que alucina en glitches temporales finos), localizar de forma barata y reproducible las
zonas/candidatos sospechosos. El VLM solo confirmará o descartará una sospecha ya localizada.

Método: diferencia absoluta entre frames consecutivos + *optical flow* denso (Farneback) y
análisis por bloques. Un cambio con **flujo bajo** (la zona no se mueve pero cambia) es parpadeo;
un cambio con **flujo disperso/incoherente** es tearing; un cambio muy alto es corrupción. El
movimiento coherente (un objeto que se desplaza) **no** se marca.

Uso:
    python tools/vision-review/temporal-detect.py --sequence <dir> [--block 16] [--out <dir>]
                                                  [--diff 25] [--flow 2.5] [--json]

Salida: `out/vision-review/<demoId>/temporal-detect.{json,md}` (o `--out`). Código de salida:
    0 = sin anomalías, 4 = hay candidatos (informativo), 2 = error de uso/entrada.

Referencia: docs/guides/methodology/PROTOCOLO_ETAPAS_GRAFICOS.md y DEMO_VISUAL_DEBUG.md.
"""
import argparse
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np


def load_frames(folder, max_frames=None):
    """Carga frames PNG/JPG de una carpeta, ordenados por nombre."""
    files = sorted(Path(folder).glob("frame_*.png"))
    if not files:
        files = sorted(Path(folder).glob("*.png")) + sorted(Path(folder).glob("*.jpg"))
    frames = []
    for f in files[:max_frames] if max_frames else files:
        img = cv2.imread(str(f))
        if img is not None:
            frames.append(img)
    return files, frames


def compute_suspicion(frames, block_size=16, flow_threshold=0.5, diff_threshold=30):
    """Analiza la secuencia y devuelve (mapas_de_sospecha, candidatos).

    Criterio primario (fiable para parpadeo): **oscilación A→B→A**. Un píxel que en `f-1` y `f+1`
    vale lo mismo pero en `f` es distinto está parpadeando; un movimiento **no** lo cumple (el valor
    se desplaza, no vuelve). El *optical flow* se usa solo como clasificador secundario:
      - oscilación alta y localizada       → `flicker`
      - cambio alto sin retorno + flujo disperso → `tearing`
      - cambio muy alto sin retorno        → `corruption`
    """
    if len(frames) < 3:
        return [], []
    h, w = frames[0].shape[:2]
    gray = [cv2.cvtColor(f, cv2.COLOR_BGR2GRAY) for f in frames]
    maps, anomalies = [], []
    for i in range(1, len(gray) - 1):
        prev, curr, nxt = gray[i - 1], gray[i], gray[i + 1]
        back = cv2.absdiff(prev, curr)                         # f-1 -> f
        fwd = cv2.absdiff(curr, nxt)                           # f   -> f+1
        # Oscilación: cambia en ambos sentidos → vuelve al valor original (A-B-A).
        osc = cv2.min(back, fwd)
        diff = cv2.addWeighted(back, 0.5, fwd, 0.5, 0)
        flow = cv2.calcOpticalFlowFarneback(
            prev, curr, None, pyr_scale=0.5, levels=3, winsize=15,
            iterations=3, poly_n=5, poly_sigma=1.2, flags=0)
        mag, _ = cv2.cartToPolar(flow[..., 0], flow[..., 1])
        mag_u8 = cv2.normalize(mag, None, 0, 255, cv2.NORM_MINMAX).astype(np.uint8)
        diff_u8 = cv2.normalize(diff, None, 0, 255, cv2.NORM_MINMAX)
        susp = cv2.GaussianBlur(cv2.addWeighted(diff_u8, 0.6, mag_u8, 0.4, 0), (5, 5), 0)
        maps.append({"frame": i, "suspicion": susp})
        for y in range(0, h, block_size):
            for x in range(0, w, block_size):
                bo = osc[y:y + block_size, x:x + block_size]
                bd = diff[y:y + block_size, x:x + block_size]
                bm = mag[y:y + block_size, x:x + block_size]
                mean_osc = float(np.mean(bo))
                mean_diff = float(np.mean(bd))
                mean_mag = float(np.mean(bm))
                std_mag = float(np.std(bm))
                kind = None
                if mean_osc > diff_threshold:
                    kind = "flicker"           # vuelve al valor original: parpadeo
                elif mean_diff > diff_threshold * 2.0:
                    kind = "corruption"
                elif mean_diff > diff_threshold and std_mag > 3.0:
                    kind = "tearing"
                if kind:
                    anomalies.append({
                        "frame": i, "x": int(x), "y": int(y),
                        "w": block_size, "h": block_size,
                        "mean_diff": round(mean_diff, 2),
                        "mean_osc": round(mean_osc, 2),
                        "mean_mag": round(mean_mag, 2),
                        "std_mag": round(std_mag, 2),
                        "type": kind,
                    })
    return maps, anomalies


def _box(d):
    """bbox normalizado (x1,y1,x2,y2) de un candidato fusionado o de un bloque crudo."""
    x2 = d["x2"] if "x2" in d else d["x"] + d["w"]
    y2 = d["y2"] if "y2" in d else d["y"] + d["h"]
    return d["x"], d["y"], x2, y2


def _overlaps(a, b, pad):
    ax1, ay1, ax2, ay2 = _box(a)
    bx1, by1, bx2, by2 = _box(b)
    return not (ax2 + pad < bx1 or bx2 + pad < ax1 or ay2 + pad < by1 or by2 + pad < ay1)


def merge_candidates(anomalies, frame_pad=1, space_pad=8, region_pad=8, frame_count=None,
                     img_w=0, img_h=0):
    """Agrupa anomalías contiguas (tiempo+espacio) en regiones candidatas con bbox y score."""
    merged = []
    for a in sorted(anomalies, key=lambda z: (z["frame"], z["y"], z["x"])):
        placed = False
        for m in merged:
            if abs(m["frame"] - a["frame"]) <= frame_pad and _overlaps(m, a, space_pad):
                m["x"] = min(m["x"], a["x"])
                m["y"] = min(m["y"], a["y"])
                m["x2"] = max(m["x2"], a["x"] + a["w"])
                m["y2"] = max(m["y2"], a["y"] + a["h"])
                m["score"] = max(m["score"], a["mean_diff"])
                m["types"].add(a["type"])
                placed = True
                break
        if not placed:
            merged.append({
                "frame": a["frame"], "x": a["x"], "y": a["y"],
                "x2": a["x"] + a["w"], "y2": a["y"] + a["h"],
                "score": a["mean_diff"], "types": {a["type"]},
            })
    out = []
    for m in merged:
        x1 = max(0, m["x"] - region_pad)
        y1 = max(0, m["y"] - region_pad)
        x2 = min(img_w, m["x2"] + region_pad)
        y2 = min(img_h, m["y2"] + region_pad)
        # Contexto temporal: frames previo/siguiente si existen.
        ctx = []
        if frame_count:
            ctx = [max(0, m["frame"] - 1), m["frame"], min(frame_count - 1, m["frame"] + 1)]
        out.append({
            "frame": m["frame"], "region": [x1, y1, x2, y2],
            "type": sorted(m["types"]), "score": round(m["score"], 2),
            "context_frames": sorted(set(ctx)),
        })
    out.sort(key=lambda z: -z["score"])
    return out


def relative_region(x, y, w, h, img_w, img_h):
    """Traduce una región a vocabulario relativo (sin números de píxel), para el VLM."""
    def third(v, total):
        return "izquierda" if v < total / 3 else ("centro" if v < 2 * total / 3 else "derecha")
    def thirdv(v, total):
        return "arriba" if v < total / 3 else ("centro" if v < 2 * total / 3 else "abajo")
    hz = third(x + w / 2, img_w)
    vt = thirdv(y + h / 2, img_h)
    if hz == "centro" and vt == "centro":
        return "centro"
    if hz == "centro":
        return vt
    if vt == "centro":
        return hz
    return f"{vt}-{hz}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sequence", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--block", type=int, default=16)
    ap.add_argument("--diff", type=float, default=30.0)
    ap.add_argument("--flow", type=float, default=0.5)
    ap.add_argument("--max-frames", type=int, default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    files, frames = load_frames(args.sequence, args.max_frames)
    if len(frames) < 3:
        print(f"[temporal-detect] {args.sequence}: <3 frames (se omite).", file=sys.stderr)
        return 3

    h, w = frames[0].shape[:2]
    maps, anomalies = compute_suspicion(frames, args.block, args.flow, args.diff)
    candidates = merge_candidates(anomalies, frame_count=len(frames), img_w=w, img_h=h)
    for c in candidates:
        x1, y1, x2, y2 = c["region"]
        c["relative"] = relative_region(x1, y1, x2 - x1, y2 - y1, w, h)

    demo_id = os.path.basename(os.path.normpath(args.sequence))
    seq_path = Path(args.sequence).resolve()
    # demoId = carpeta dos niveles por encima de `sequence` (out/run/<demoId>/<config>/sequence);
    # si no aplica, se usa el nombre de la carpeta padre.
    parts = seq_path.parts
    demo_id = parts[-3] if len(parts) >= 3 else seq_path.parent.name
    out_dir = args.out or os.path.join("out", "vision-review", demo_id)
    os.makedirs(out_dir, exist_ok=True)

    report = {
        "sequence": args.sequence.replace("\\", "/"),
        "demo": demo_id,
        "frames": len(frames),
        "size": [w, h],
        "params": {"block": args.block, "diff": args.diff, "flow": args.flow},
        "anomaly_blocks": len(anomalies),
        "candidates": candidates,
    }
    with open(os.path.join(out_dir, "temporal-detect.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)

    lines = [
        f"# Detección temporal (determinista) — {demo_id}",
        "",
        f"Secuencia: `{report['sequence']}` · {len(frames)} frames · {w}×{h} px · "
        f"bloque {args.block} px · {len(anomalies)} bloques anómalos → {len(candidates)} candidatos.",
        "",
    ]
    if candidates:
        lines += ["| frame | región (px) | zona | tipo | score |", "|---|---|---|---|---|"]
        for c in candidates:
            x1, y1, x2, y2 = c["region"]
            lines.append(f"| {c['frame']} | {x1},{y1}–{x2},{y2} | {c['relative']} | "
                         f"{','.join(c['type'])} | {c['score']} |")
        lines += ["", "> Candidatos deterministas: solo estos tramos/regiones se envían al modelo de "
                      "visión, con frames de contexto. El movimiento coherente (objetos que se "
                      "desplazan) no debería aparecer aquí; si aparece, revisar umbrales."]
    else:
        lines += ["Sin anomalías temporales por encima del umbral."]
    with open(os.path.join(out_dir, "temporal-detect.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    if args.json:
        print(json.dumps(report, ensure_ascii=False))
    else:
        print(f"[temporal-detect] {demo_id}: {len(candidates)} candidato(s) "
              f"({len(anomalies)} bloques); informe: {out_dir}/temporal-detect.md")
    return 4 if candidates else 0


if __name__ == "__main__":
    sys.exit(main())
