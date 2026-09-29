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


def _is_motion(prev, curr, x, y, bs, search):
    """¿El bloque `curr[y:y+bs, x:x+bs]` es contenido **desplazado** (movimiento legítimo)?

    Dos señales:
      1. El bloque se parece mucho a la **misma posición** de `prev` (solo entra/sale un borde de
         1 px: cambio sub-píxel de un objeto móvil) → movimiento.
      2. El bloque aparece en `prev` **desplazado** dentro de ±`search` px (traslación) → movimiento.
    Un parpadeo puro cambia el bloque a un valor/patrón que **no** está ni en su posición ni cerca,
    así que no se descarta. Resuelve los falsos positivos en bordes de objetos que se mueven.
    """
    h, w = curr.shape
    x0, y0 = max(0, x - search), max(0, y - search)
    x1, y1 = min(w, x + bs + search), min(h, y + bs + search)
    if x1 - x0 < bs or y1 - y0 < bs:
        return False
    block = curr[y:y + bs, x:x + bs]
    # Un bloque **plano** (sin estructura) no permite decidir por contenido (un parpadeo de color
    # plano coincide trivialmente con zonas planas): se omite el matcher.
    if float(np.std(block)) < 6.0:
        return False
    region = prev[y0:y1, x0:x1]
    res = cv2.matchTemplate(region, block, cv2.TM_SQDIFF_NORMED)
    _, best, minloc, _ = cv2.minMaxLoc(res)
    dx = (x0 + minloc[0]) - x
    dy = (y0 + minloc[1]) - y
    # (1) mismo sitio, muy parecido; (2) mismo contenido desplazado.
    return best < 0.10


def _ssim(a, b):
    """SSIM medio de dos bloques (uint8) con la fórmula clásica (medias/varianzas). NumPy SIMD."""
    a = a.astype(np.float64)
    b = b.astype(np.float64)
    C1 = (0.01 * 255.0) ** 2
    C2 = (0.03 * 255.0) ** 2
    mu_a, mu_b = a.mean(), b.mean()
    va, vb = a.var(), b.var()
    cov = ((a - mu_a) * (b - mu_b)).mean()
    return ((2 * mu_a * mu_b + C1) * (2 * cov + C2)) / \
           ((mu_a * mu_a + mu_b * mu_b + C1) * (va + vb + C2))


def compute_suspicion(frames, block_size=16, flow_threshold=0.5, diff_threshold=30,
                      ssim_structural=0.85):
    """Analiza la secuencia y devuelve (mapas_de_sospecha, candidatos).

    Criterio primario (fiable para parpadeo): **oscilación A→B→A**. Un píxel que en `f-1` y `f+1`
    vale lo mismo pero en `f` es distinto está parpadeando; un movimiento **no** lo cumple (el valor
    se desplaza, no vuelve). Se añade un **matcher de vecindad** (`_is_motion`) que descarta los
    bloques que simplemente se han desplazado (bordes de objetos móviles), y el *optical flow* como
    clasificador secundario de tearing/corruption.
    """
    if len(frames) < 3:
        return [], []
    h, w = frames[0].shape[:2]
    gray = [cv2.cvtColor(f, cv2.COLOR_BGR2GRAY) for f in frames]
    maps, anomalies = [], []
    search = max(2, block_size // 2)  # ventana de búsqueda del matcher (±px)
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
        # Bloques con **solape** (paso = mitad del bloque): un artefacto pequeño no alineado a la
        # rejilla cae dentro de al menos un bloque con su superficie completa (si no, se diluye).
        step = max(4, block_size // 2)
        for y in range(0, h - block_size + 1, step):
            for x in range(0, w - block_size + 1, step):
                bo = osc[y:y + block_size, x:x + block_size]
                bb = back[y:y + block_size, x:x + block_size]
                bf = fwd[y:y + block_size, x:x + block_size]
                bd = diff[y:y + block_size, x:x + block_size]
                bm = mag[y:y + block_size, x:x + block_size]
                mean_osc = float(np.mean(bo))
                mean_diff = float(np.mean(bd))
                # Dirección del cambio: una **aparición/desaparición** (flicker de 1 frame, corrupción
                # que entra) se ve en `back` o `fwd`, no en su media. Se usa el máximo.
                mean_jump = max(float(np.mean(bb)), float(np.mean(bf)))
                mean_mag = float(np.mean(bm))
                std_mag = float(np.std(bm))
                # Cambio alto: decidir si es movimiento (bloque desplazado) o glitch.
                if max(mean_osc, mean_jump) <= diff_threshold:
                    continue
                if max(mean_osc, mean_jump) <= diff_threshold * 2.0 and \
                   (_is_motion(prev, curr, x, y, block_size, search) or
                        _is_motion(nxt, curr, x, y, block_size, search)):
                    continue  # cambio moderado con contenido desplazado → movimiento
                kind = None
                if mean_osc > diff_threshold:
                    kind = "flicker"           # vuelve al valor original: parpadeo
                elif mean_jump > diff_threshold * 3.0:
                    kind = "corruption"        # cambio muy alto en una dirección: aparece/desaparece
                elif mean_jump > diff_threshold * 2.0 and std_mag > 5.0:
                    kind = "tearing"           # cambio alto con flujo disperso (línea rasgada)
                if kind:
                    # SSIM del bloque (curr vs prev): confirma cambio **estructural**. Si el
                    # contenido se parece (SSIM alto) pese al diff, es variación de brillo/ruido,
                    # no un glitch estructural → se descarta para no dar falsos positivos.
                    s = _ssim(prev[y:y + block_size, x:x + block_size],
                              curr[y:y + block_size, x:x + block_size])
                    if s < ssim_structural or kind == "flicker":
                        anomalies.append({
                            "frame": i, "x": int(x), "y": int(y),
                            "w": block_size, "h": block_size,
                            "mean_diff": round(mean_diff, 2),
                            "mean_jump": round(mean_jump, 2),
                            "mean_osc": round(mean_osc, 2),
                            "mean_mag": round(mean_mag, 2),
                            "std_mag": round(std_mag, 2),
                            "ssim": round(float(s), 4),
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
            if abs(m["frame"] - a["frame"]) <= frame_pad and _overlaps(m, a, space_pad) and \
               (a["type"] in m["types"] or not m["types"]):
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
    ap.add_argument("--ssim", type=float, default=0.85)
    ap.add_argument("--max-frames", type=int, default=None)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    files, frames = load_frames(args.sequence, args.max_frames)
    if len(frames) < 3:
        print(f"[temporal-detect] {args.sequence}: <3 frames (se omite).", file=sys.stderr)
        return 3

    h, w = frames[0].shape[:2]
    maps, anomalies = compute_suspicion(frames, args.block, args.flow, args.diff, args.ssim)
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
