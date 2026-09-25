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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sequence", required=True)
    ap.add_argument("--out", default=None)
    ap.add_argument("--thresh", type=int, default=40)
    ap.add_argument("--metric", choices=["diff", "ssim", "both"], default="both")
    ap.add_argument("--block", type=int, default=16)
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    files, frames = load_frames(args.sequence)
    if len(frames) < 2:
        print(f"[frame-diff] {args.sequence}: <2 frames (se omite).", file=sys.stderr)
        return 3
    h, w = frames[0].shape[:2]

    pairs = []
    for f in range(1, len(frames)):
        entry = {"from": f - 1, "to": f}
        if args.metric in ("diff", "both"):
            entry.update(diff_pair(frames[f - 1], frames[f], args.thresh))
        if args.metric in ("ssim", "both"):
            ga = cv2.cvtColor(frames[f - 1], cv2.COLOR_BGR2GRAY)
            gb = cv2.cvtColor(frames[f], cv2.COLOR_BGR2GRAY)
            entry.update(ssim_score(ga, gb, args.block))
        pairs.append(entry)

    seq_path = Path(args.sequence).resolve()
    demo_id = seq_path.parts[-3] if len(seq_path.parts) >= 3 else seq_path.parent.name
    out_dir = args.out or os.path.join("out", "vision-review", demo_id)
    os.makedirs(out_dir, exist_ok=True)

    report = {
        "sequence": args.sequence.replace("\\", "/"),
        "demo": demo_id,
        "frames": len(frames),
        "size": [w, h],
        "metric": args.metric,
        "thresh": args.thresh,
        "block": args.block,
        "pairs": pairs,
    }
    with open(os.path.join(out_dir, "frame-diff.json"), "w", encoding="utf-8") as fp:
        json.dump(report, fp, indent=2, ensure_ascii=False)

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
