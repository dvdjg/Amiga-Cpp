#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador de la demo 082_plasma (copper chunky).
#
# No hay overlay de depuracion: el plasma es un patron naranja/amarillo/cyan. Exige
# amarillo (hay zonas calidas) y contenido no-azul-Workbench, sin pedir texto blanco.
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
