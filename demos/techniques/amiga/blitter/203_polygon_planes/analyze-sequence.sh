#!/usr/bin/env bash
# Gate visual temporal de la demo 203: captura una secuencia y verifica que el
# solido gira (cambia de forma entre frames). Lo invoca tools/test-regression.sh.
#
# Uso: demos/techniques/amiga/blitter/203_polygon_planes/analyze-sequence.sh [--warp] [...]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/blitter/203_polygon_planes"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 6 --sequence-interval-ms 400)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run/203_polygon_planes" -maxdepth 2 -type d -name sequence 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 203" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-203-polygon-planes.mjs" --sequence-dir "$SEQ"
