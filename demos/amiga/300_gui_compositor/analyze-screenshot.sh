#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 300 (compositor de `eng::ui`).
#
# Sustituye al analizador generico (que exige el overlay de texto verde/amarillo): esta demo no
# dibuja overlay, sino ventanas movibles. Se comprueba la paleta de la escena (relleno de ventana,
# barra de titulo, texto) y, si hay secuencia, que las ventanas se mueven entre frames.
#
# Uso: demos/amiga/300_gui_compositor/analyze-screenshot.sh <imagen.png>
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
	exec node "$ROOT/tools/analyze/verify-gui-compositor.mjs" --sequence-dir "$SEQD"
fi
exec node "$ROOT/tools/analyze/verify-gui-compositor.mjs" --image "$IMAGE"
