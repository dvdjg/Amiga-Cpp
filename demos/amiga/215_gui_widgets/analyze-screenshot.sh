#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 215 (widgets de `eng::ui`).
#
# Sustituye al analizador generico (que exige el overlay de texto verde/amarillo):
# esta demo no dibuja overlay, sino una UI real, y se comprueba la paleta de la
# escena (panel, texto, bisel y anillo de foco) con tools/analyze/verify-gui-widgets.mjs.
#
# Si existe la secuencia de capturas (run-demo.sh --sequence-frames N), se analiza
# la secuencia: exige que la pista del slider cambie entre frames y que el cambio
# se concentre en una banda horizontal (repintado por zona).
#
# Uso: demos/amiga/215_gui_widgets/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

SEQD="$(dirname "$IMAGE")/sequence"
if [ -d "$SEQD" ]; then
	exec node "$ROOT/tools/analyze/verify-gui-widgets.mjs" --sequence-dir "$SEQD"
fi
exec node "$ROOT/tools/analyze/verify-gui-widgets.mjs" --image "$IMAGE"
