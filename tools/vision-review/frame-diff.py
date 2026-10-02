#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Frame-diff y SSIM por bloques, vectorizados (NumPy/OpenCV usan SIMD: SSE/AVX).

Referencia determinista para separar **movimiento** de **glitch** y para medir cambio
**estructural** (contenido) frente a cambio de brillo. Sustituye el bucle por píxel de
`frame-diff.mjs` con operaciones de array (mucho más rápido) y añade:

  - `diff`  : píxeles cambiados (|ΔR|+|ΔG|+|ΔB| > thr) y su bbox, por par de frames.
  - `ssim`  : SSIM medio por par (1.0 = idéntico) y nº de bloques con SSIM bajo (cambio
              estructural real, no solo brillo/desplazamiento).
  - `both`  : las dos métricas.

Uso:
    python tools/vision-review/frame-diff.py --sequence <dir> [--thresh 40]
                                             [--metric diff|ssim|both] [--block 16] [--json]
Salida: por pantalla y `out/vision-review/<demoId>/frame-diff.{json,md}`. Código de salida:
    0 = ok, 2 = uso/entrada, 3 = secuencia insuficiente.

Requiere `opencv-python` y `numpy` (dependencia opcional; ver docs/build/BUILD_AND_RUN.md).
"""
import argparse
import importlib.util
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np


def load_frames(folder, max_frames=None):
    """Carga frames PNG de una carpeta, ordenados por nombre."""
    files = sorted(Path(folder).glob("frame_*.png"))
    frames = []
    for f in (files[:max_frames] if max_frames else files):
        img = cv2.imread(str(f))
        if img is not None:
            frames.append(img)
    return files, frames


def diff_pair(a, b, thr):
    """Píxeles cambiados (|ΔR|+|ΔG|+|ΔB| > thr) y bbox. Vectorizado (SIMD)."""
    # int16 para evitar desbordes al sumar canales.
    d = np.abs(a.astype(np.int16) - b.astype(np.int16)).sum(axis=2)
    mask = d > thr
    n = int(mask.sum())
    if n == 0:
        return {"changed": 0, "bbox": None, "mean": float(d.mean())}
    ys, xs = np.where(mask)
    return {
        "changed": n,
        "bbox": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())],
        "mean": round(float(d.mean()), 3),
    }


def ssim_score(gray_a, gray_b, block):
    """SSIM medio global y nº de bloques con SSIM bajo (cambio estructural).

    SSIM clásico con medias/varianzas gaussianas. `block` px por lado para el recuento
    de bloques estructuralmente distintos (evita que un pequeño cambio domine el global).
    """
    a = gray_a.astype(np.float64)
    b = gray_b.astype(np.float64)
    C1 = (0.01 * 255.0) ** 2
    C2 = (0.03 * 255.0) ** 2
    k = (11, 11)
    mu_a = cv2.GaussianBlur(a, k, 1.5)
    mu_b = cv2.GaussianBlur(b, k, 1.5)
    mu_a2, mu_b2, mu_ab = mu_a * mu_a, mu_b * mu_b, mu_a * mu_b
    sig_a2 = cv2.GaussianBlur(a * a, k, 1.5) - mu_a2
    sig_b2 = cv2.GaussianBlur(b * b, k, 1.5) - mu_b2
    sig_ab = cv2.GaussianBlur(a * b, k, 1.5) - mu_ab
    ssim_map = ((2 * mu_ab + C1) * (2 * sig_ab + C2)) / ((mu_a2 + mu_b2 + C1) * (sig_a2 + sig_b2 + C2))
    mean = float(ssim_map.mean())
    # Bloques con SSIM bajo (cambio estructural localizado).
    h, w = ssim_map.shape
    low = 0
    for y in range(0, max(1, h - block + 1), block):
        for x in range(0, max(1, w - block + 1), block):
            if float(ssim_map[y:y + block, x:x + block].mean()) < 0.85:
                low += 1
    return {"ssim": round(mean, 5), "blocks_low": low}


def temporal_low_blocks(gray_a, gray_b, block, threshold=8.0, ignore_rois=()):
    """Cuenta bloques con cambio residual tras compensar paneo, descontando ROIs explícitas."""
    delta = cv2.absdiff(gray_a, gray_b)
    h, w = delta.shape
    low = 0
    for by in range(0, max(1, h - block + 1), block):
        for bx in range(0, max(1, w - block + 1), block):
            x_abs, y_abs = bx, by
            ignored = any(x_abs >= x and x_abs + block <= x + iw and
                          y_abs >= y and y_abs + block <= y + ih
                          for x, y, iw, ih in ignore_rois)
            if not ignored and float(delta[by:by + block, bx:bx + block].mean()) > threshold:
                low += 1
    return low


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sequence", default=None)
    ap.add_argument("--files", nargs="+", default=None,
                    help="lista exacta de frames en orden")
    ap.add_argument("--out", default=None)
    ap.add_argument("--thresh", type=int, default=40)
    ap.add_argument("--metric", choices=["diff", "ssim", "both"], default="both")
    ap.add_argument("--block", type=int, default=16)
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--stdout-json", action="store_true",
                    help="escribe el informe JSON a stdout sin guardar el reporte")
    ap.add_argument("--compensate-global-motion", action="store_true",
                    help="compensa traslación global coherente antes de contar cambios estructurales")
    ap.add_argument("--ignore-roi", action="append", default=[], metavar="X,Y,W,H",
                    help="excluye una región explícita del recuento SSIM (se repite por región)")
    ap.add_argument("--heatmap", action="store_true",
                    help="escribe un PNG de mapa de calor de las diferencias (para el VLM)")
    args = ap.parse_args()
    parsed_ignore_rois = []
    for region in args.ignore_roi:
        try:
            parsed_ignore_rois.append(tuple(int(v) for v in region.split(",")))
        except (TypeError, ValueError):
            ap.error(f"--ignore-roi debe ser X,Y,W,H: {region}")

    if not args.sequence and not args.files:
        ap.error("se requiere --sequence o --files")
    if args.files:
        files = [Path(p) for p in args.files]
        frames = [cv2.imread(str(f)) for f in files]
        pairs = [(f, img) for f, img in zip(files, frames) if img is not None]
        files, frames = [x[0] for x in pairs], [x[1] for x in pairs]
    else:
        files, frames = load_frames(args.sequence)
    if len(frames) < 2:
        print(f"[frame-diff] {args.sequence}: <2 frames (se omite).", file=sys.stderr)
        return 3
    gray_frames = [cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY).astype(np.float32)
                   for frame in frames]
    motion_alignment = {"applied": False, "reason": "disabled"}
    if args.compensate_global_motion:
        detector_path = Path(__file__).with_name("temporal-detect.py")
        spec = importlib.util.spec_from_file_location("temporal_detect", detector_path)
        detector = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(detector)
        _, motion_alignment = detector.align_global_motion(frames)
    h, w = frames[0].shape[:2]

    pairs = []
    for f in range(1, len(frames)):
        entry = {"from": f - 1, "to": f}
        if args.metric in ("diff", "both"):
            entry.update(diff_pair(frames[f - 1], frames[f], args.thresh))
        if args.metric in ("ssim", "both"):
            ga = cv2.cvtColor(frames[f - 1], cv2.COLOR_BGR2GRAY)
            gb = cv2.cvtColor(frames[f], cv2.COLOR_BGR2GRAY)
            score = ssim_score(ga, gb, args.block)
            if args.compensate_global_motion:
                # Con paneo de ±4 px/frame, un cambio grande residual apunta a un evento local
                # (p. ej. wrap de tilemap o tearing), no al movimiento general ya compensado.
                (dx, dy), response = cv2.phaseCorrelate(
                    gray_frames[f - 1], gray_frames[f])
                margin = max(16, int(np.ceil(max(abs(dx), abs(dy)))) + 2)
                if response >= 0.15 and abs(dx) <= w / 4 and abs(dy) <= h / 4 and \
                   h > 2 * margin and w > 2 * margin:
                    # Recorta el perímetro de tiles: el wrap del mapa puede cambiar la columna
                    # visible de manera legítima; las zonas internas siguen bajo el gate.
                    # Alinea con el desplazamiento entero visible al muestrear bitplanes:
                    # la correlación de fase aporta la estimación de traslación global.
                    integer_dx, integer_dy = int(round(dx)), int(round(dy))
                    aligned_integer = cv2.warpAffine(
                        frames[f],
                        np.float32([[1.0, 0.0, -integer_dx], [0.0, 1.0, -integer_dy]]),
                        (w, h), flags=cv2.INTER_NEAREST,
                        borderMode=cv2.BORDER_REPLICATE)
                    integer_gray = cv2.cvtColor(aligned_integer, cv2.COLOR_BGR2GRAY)
                    roi_a = ga[margin:h - margin, margin:w - margin]
                    roi_b = integer_gray[margin:h - margin, margin:w - margin]
                    # Elimina bloques enteros comprendidos en ROIs justificadas por la demo.
                    # Se amplían 16 px para absorber el borde del matcher tras una traslación.
                    exclusion_pad = 16
                    low = temporal_low_blocks(
                        roi_a, roi_b, args.block, ignore_rois=[
                            (x - margin - exclusion_pad, y - margin - exclusion_pad,
                             iw + exclusion_pad * 2, ih + exclusion_pad * 2)
                            for x, y, iw, ih in parsed_ignore_rois])
                    score.update({
                        "ssim": ssim_score(roi_a, roi_b, args.block)["ssim"],
                        "blocks_low": low,
                    })
                    score["global_dx"] = round(float(dx), 3)
                    score["global_dy"] = round(float(dy), 3)
                    score["global_response"] = round(float(response), 4)
                    score["compensated_margin"] = margin
            entry.update(score)
        pairs.append(entry)

    seq_path = Path(args.sequence).resolve() if args.sequence else files[0].resolve().parent
    demo_id = seq_path.parts[-3] if len(seq_path.parts) >= 3 else seq_path.parent.name
    out_dir = args.out or os.path.join("out", "vision-review", demo_id)
    os.makedirs(out_dir, exist_ok=True)

    report = {
        "sequence": (args.sequence or str(seq_path)).replace("\\", "/"),
        "demo": demo_id,
        "frames": len(frames),
        "size": [w, h],
        "metric": args.metric,
        "thresh": args.thresh,
        "block": args.block,
        "motion_alignment": motion_alignment,
        "pairs": pairs,
    }
    if args.stdout_json:
        print(json.dumps(report, ensure_ascii=False))
        return 0
    with open(os.path.join(out_dir, "frame-diff.json"), "w", encoding="utf-8") as fp:
        json.dump(report, fp, indent=2, ensure_ascii=False)

    # Mapa de calor: acumula |Δ| de todos los pares y lo colorea (JET) sobre el último frame.
    # Sirve para enseñar al VLM exactamente *dónde* cambió (evita coordenadas inventadas).
    heat_path = None
    if args.heatmap and len(frames) >= 2:
        acc = np.zeros((h, w), dtype=np.float64)
        for f in range(1, len(frames)):
            acc += np.abs(frames[f - 1].astype(np.int16) - frames[f].astype(np.int16)).sum(axis=2)
        acc = np.clip(acc / max(1, len(frames) - 1), 0, 255)
        heat = cv2.applyColorMap(acc.astype(np.uint8), cv2.COLORMAP_JET)
        overlay = cv2.addWeighted(frames[-1], 0.4, heat, 0.6, 0)
        heat_path = os.path.join(out_dir, "heat-diff.png")
        cv2.imwrite(heat_path, overlay)

    cols = ["par"]
    if args.metric in ("diff", "both"):
        cols += ["px cambiados", "bbox", "Δ medio"]
    if args.metric in ("ssim", "both"):
        cols += ["SSIM", "bloques bajos"]
    lines = [f"| {' | '.join(cols)} |", "|" + "---|" * len(cols)]
    for p in pairs:
        row = [f"f{p['from']}→f{p['to']}"]
        if args.metric in ("diff", "both"):
            bb = p["bbox"]
            row += [str(p["changed"]), f"{bb[0]},{bb[1]}–{bb[2]},{bb[3]}" if bb else "(sin cambio)", str(p["mean"])]
        if args.metric in ("ssim", "both"):
            row += [str(p["ssim"]), str(p["blocks_low"])]
        lines.append("| " + " | ".join(row) + " |")

    md = "\n".join([
        f"# Frame-diff / SSIM — {demo_id}",
        "",
        f"Secuencia: `{report['sequence']}` · {len(frames)} frames · {w}×{h} px · "
        f"métrica `{args.metric}` · umbral diff {args.thresh} · bloque {args.block}.",
        "",
        *lines,
        "",
        "> `diff` mide píxeles cambiados (movimiento o glitch); `SSIM` mide cambio **estructural** "
        "(1.0 = idéntico). Un cambio localizado con SSIM alto y diff alto suele ser movimiento.",
        *( [f"", f"Mapa de calor: `{os.path.basename(heat_path)}`"] if heat_path else [] ),
    ]) + "\n"
    with open(os.path.join(out_dir, "frame-diff.md"), "w", encoding="utf-8") as fp:
        fp.write(md)

    if args.json:
        print(json.dumps(report, ensure_ascii=False))
    else:
        print(f"[frame-diff] {demo_id}: {len(pairs)} pares; informe: {out_dir}/frame-diff.md")
    return 0


if __name__ == "__main__":
    sys.exit(main())
