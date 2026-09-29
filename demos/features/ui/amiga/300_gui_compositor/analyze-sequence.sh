#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 300 (compositor de `eng::ui`): captura una secuencia con el runner y
# verifica que las ventanas se pintan (relleno + titulo + texto) y que se mueven entre frames.
# La invoca tools/test-regression.sh (puede recibir --warp; aqui solo se reenvia --warp).
#
# Uso: demos/features/ui/amiga/300_gui_compositor/analyze-sequence.sh [--warp] [...]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/300_gui_compositor"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 4 --sequence-interval-ms 400)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*300_gui_compositor*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 300" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-gui-compositor.mjs" --sequence-dir "$SEQ"
