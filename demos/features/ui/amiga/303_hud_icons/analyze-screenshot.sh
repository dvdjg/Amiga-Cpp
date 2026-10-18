#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 303 (inventario con seleccion y foco).
#
# Comprueba la franja del HUD (texto, objetos de la rejilla y marco del foco) y,
# si hay secuencia, que la celda seleccionada cambia, con
# tools/analyze/verify-hud-icons.mjs.
#
# Uso: demos/features/ui/amiga/303_hud_icons/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

SEQD="$(dirname "$IMAGE")/sequence"
if [ -d "$SEQD" ]; then
	exec node "$ROOT/tools/analyze/verify-hud-icons.mjs" --sequence-dir "$SEQD"
fi
exec node "$ROOT/tools/analyze/verify-hud-icons.mjs" --image "$IMAGE"
