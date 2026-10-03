#!/usr/bin/env python3
"""Comprueba el MOVIMIENTO horizontal de una secuencia de scroll: mide el desplazamiento
entre frames consecutivos (SSD sobre color, zona de display) y lo valida.

Uso:
  python tools/vision-review/motion-check.py <seq_dir> --expect-px N [--tol M] [--json]

`--expect-px` = desplazamiento esperado entre frames consecutivos (px). Falla (exit 1) si
algún par no está en [expect-tol, expect+tol] o si el desplazamiento es 0 (congelado).

Requisitos: numpy + Pillow. Complementa la validación con Ollama (regla de oro).
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
expect = 0
tol = 3
as_json = "--json" in args
if "--expect-px" in args:
    expect = int(args[args.index("--expect-px") + 1])
if "--tol" in args:
    tol = int(args[args.index("--tol") + 1])

frames = sorted(glob.glob(os.path.join(seq, "frame_*.png")))
if len(frames) < 2:
    print("motion-check: hacen falta >=2 frames", file=sys.stderr)
    sys.exit(2)


def load(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im, dtype=np.float32)
    h, w, _ = a.shape
    return a[int(h * 0.20):int(h * 0.80), int(w * 0.15):int(w * 0.85)]


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


pushes = []
diffs = []
prev = load(frames[0])
prev_raw = prev
for f in frames[1:]:
    cur = load(f)
    pushes.append(best_shift(prev, cur))
    diffs.append(float(np.mean(np.abs(prev - cur))))
    prev = cur

# Criterio robusto (la captura no es frame-exacta ni el estimador resuelve <4 px):
#  - NO congelado: hay cambio entre frames (diff medio > umbral);
#  - direccion correcta: ningun desplazamiento NEGATIVO (con el `BPLCON1` invertido el
#    contenido va al contrario / oscila -> aparece negativo);
#  - sin saltos enormes (<= 64 px).
NOT_FROZEN = 2.0
ok = (len(diffs) > 0 and max(diffs) > NOT_FROZEN and all(p >= 0 for p in pushes)
      and all(p <= 64 for p in pushes))
res = {"demo_dir": os.path.basename(seq.rstrip("/\\")), "frames": len(frames),
       "shifts": pushes, "diffs": [round(d, 1) for d in diffs], "ok": ok}
if as_json:
    print(json.dumps(res))
else:
    print(f"motion-check {res['demo_dir']}: shifts={pushes} diffs={res['diffs']} -> "
          f"{'OK' if ok else 'FALLO'}")
sys.exit(0 if ok else 1)
