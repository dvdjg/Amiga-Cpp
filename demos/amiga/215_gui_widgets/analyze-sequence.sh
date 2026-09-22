#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 215 (widgets de `eng::ui`): captura una secuencia con el
# runner y verifica que la UI se pinta (panel/texto/bisel/foco) y que el slider
# anima. La invoca tools/test-regression.sh (puede recibir --warp; aqui solo se
# reenvia --warp al runner).
#
# Uso: demos/amiga/215_gui_widgets/analyze-sequence.sh [--warp] [...]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEMO="demos/amiga/215_gui_widgets"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 4 --sequence-interval-ms 250)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

# La secuencia vive en out/run/<demo>/<CONFIG_ID>/sequence.
SEQ="$(find "$ROOT/out/run/215_gui_widgets" -maxdepth 2 -type d -name sequence 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 215" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-gui-widgets.mjs" --sequence-dir "$SEQ"
