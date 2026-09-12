#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador propio de la demo 110_ylimited_shooter.
#
# El analizador generico exige overlay de depuracion (texto verde/amarillo/
# blanco). Esta demo es un tilemap azul sin overlay: se exige contenido
# sustancial (tiles) y se relajan blanco/oscuro, sin pedir verde/amarillo.
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
	--image "$IMAGE" --min-white 0 --min-dark 0 --min-nonblue 10000
