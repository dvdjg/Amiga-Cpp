#!/usr/bin/env python3
"""Comprueba el MOVIMIENTO horizontal de una secuencia de scroll.

Dos modos:
  - Marcador (recomendado): `--track R,G,B` localiza el color del **marcador unico** en cada
    frame (centroide x) y comprueba que el desplazamiento entre frames es **uniforme** (baja
    dispersion) y con la **direccion correcta**. Mide el paso exacto.
  - SSD (fallback): sin `--track`, busca el desplazamiento por diferencia (para patrones claros).

Uso:
  python tools/vision-review/motion-check.py <seq_dir> [--track R,G,B] [--tol C]
         [--expect-px N] [--ptol M] [--json]

Falla (exit 1) si congelado, direccion incorrecta o (con `--expect-px`) paso fuera de `--ptol`.
Requisitos: numpy + Pillow. Complementa la validacion con vision (Ollama, regla de oro).
"""
import glob
import json
import os
import sys

try:
    import numpy as np
    from PIL import Image
except Exception as exc:  # pragma: no cover
    print("motion-check: faltan numpy/Pillow:", exc, file=sys.stderr)
    sys.exit(2)

args = sys.argv[1:]
if not args or args[0] in ("-h", "--help"):
    print(__doc__)
    sys.exit(0)
seq = args[0]
as_json = "--json" in args
track = None
if "--track" in args:
    track = [int(v) for v in args[args.index("--track") + 1].split(",")]
ctol = int(args[args.index("--tol") + 1]) if "--tol" in args else 60
expect = int(args[args.index("--expect-px") + 1]) if "--expect-px" in args else None
ptol = int(args[args.index("--ptol") + 1]) if "--ptol" in args else 4

frames = sorted(glob.glob(os.path.join(seq, "frame_*.png")))
if len(frames) < 2:
    print("motion-check: hacen falta >=2 frames", file=sys.stderr)
    sys.exit(2)


def load(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im, dtype=np.float32)
    h, w, _ = a.shape
    return a[int(h * 0.20):int(h * 0.80), int(w * 0.15):int(w * 0.85)]


def track_x(a):
    if track is None:
        return None
    d = np.abs(a - np.array(track, dtype=np.float32)).max(axis=2)
    m = d < ctol
    if int(m.sum()) < 20:
        return None
    ys, xs = np.where(m)
    return float(xs.mean())


def best_shift(a, b, max_shift=40):
    h, w, _ = a.shape
    best = (1e30, 0)
    for s in range(0, max_shift + 1):
        if w - s < 64:
            break
        d = float(np.mean(np.abs(a[:, : w - s] - b[:, s:])))
        if d < best[0]:
            best = (d, s)
    return best[1]


shifts = []
if track is not None:
    xs = [track_x(load(f)) for f in frames]
    for i in range(1, len(xs)):
        if xs[i - 1] is not None and xs[i] is not None:
            shifts.append(xs[i - 1] - xs[i])  # scroll a la izquierda -> x disminuye -> positivo
else:
    prev = load(frames[0])
    for f in frames[1:]:
        cur = load(f)
        shifts.append(best_shift(prev, cur))
        prev = cur

if not shifts:
    print("motion-check: marcador no visible en la secuencia", file=sys.stderr)
    sys.exit(2)

mean = sum(shifts) / len(shifts)
spread = max(shifts) - min(shifts)
checks = [all(s > 0 for s in shifts), spread <= max(2.0, abs(mean) * 0.5)]
if expect is not None:
    checks.append(all(abs(s - expect) <= ptol for s in shifts))
ok = all(checks)
res = {"demo_dir": os.path.basename(seq.rstrip("/\\")), "frames": len(frames),
       "shifts": [round(s, 1) for s in shifts], "mean": round(mean, 1),
       "spread": round(spread, 1), "expect": expect, "ptol": ptol, "ok": ok}
if as_json:
    print(json.dumps(res))
else:
    print(f"motion-check {res['demo_dir']}: shifts={res['shifts']} mean={mean:.1f} spread={spread:.1f}"
          f"{' expect=%d±%d' % (expect, ptol) if expect is not None else ''} -> {'OK' if ok else 'FALLO'}")
sys.exit(0 if ok else 1)
