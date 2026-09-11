#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual temporal de la demo 077: captura una secuencia con el runner y
# verifica que el cubo 3D cambia entre frames (animacion) y usa sombreado por
# profundidad. La invoca tools/test-regression.sh (puede recibir --warp,
# --pixel-assert, --vision-review...; aqui solo se reenvia --warp al runner).
#
# Uso: demos/amiga/077_math3d_cube/analyze-sequence.sh [--warp] [...]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEMO="demos/amiga/077_math3d_cube"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 8 --sequence-interval-ms 250)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

# La secuencia vive en out/run/<demo>/<CONFIG_ID>/sequence.
SEQ="$(find "$ROOT/out/run/077_math3d_cube" -maxdepth 2 -type d -name sequence 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 077" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-math3d-cube.mjs" --sequence-dir "$SEQ"
