#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual temporal de la demo 078: captura una secuencia y verifica que el
# solido 3D cambia entre frames y usa sombreado por profundidad.
# La invoca tools/test-regression.sh (reenvia --warp al runner).
#
# Uso: demos/amiga/078_math3d_solid/analyze-sequence.sh [--warp] [...]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEMO="demos/amiga/078_math3d_solid"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 8 --sequence-interval-ms 250)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run/078_math3d_solid" -maxdepth 2 -type d -name sequence 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 078" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-math3d-cube.mjs" --sequence-dir "$SEQ"
