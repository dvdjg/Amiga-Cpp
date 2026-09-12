#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador propio de la demo 080_fire_rgb.
#
# El analizador generico (tools/analyze/analyze-screenshot.sh) exige el overlay
# de depuracion (texto verde/amarillo/blanco). Esta demo no dibuja overlay: muestra
# fuego (naranja/amarillo) sobre negro, asi que exige amarillo (color de la llama)
# y permite fondo oscuro, sin exigir texto blanco ni verde.
#
# Uso: tools/analyze/analyze-screenshot.sh <imagen.png> (invocado por analyze-demo.sh)
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --min-white 0 --min-dark 0 --need-yellow
