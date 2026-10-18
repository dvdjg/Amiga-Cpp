#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion de la captura unica de la demo 080 (contenido + HUD).
#
# Comprueba, sobre la captura del runner: sala y paneles con el color unico 12
# presentes, fondo de escena y texto blanco en la franja del HUD (208..255).
# Si junto a la imagen hay una `sequence/`, delega el contrato de paneo.
#
# Uso: demos/techniques/amiga/3d/080_camera_pan_window/analyze-screenshot.sh <imagen.png>
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
	exec node "$ROOT/tools/analyze/verify-080-pan-window.mjs" --sequence-dir "$SEQD"
fi
exec node "$ROOT/tools/analyze/verify-080-pan-window.mjs" --image "$IMAGE"
