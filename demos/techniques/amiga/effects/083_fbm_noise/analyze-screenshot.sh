#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador de la demo 083_fbm_noise (copper chunky con mapa de altura fbm).
#
# No hay overlay: exige contenido no-azul-Workbench (el campo pinta agua/verde/roca/
# nieve) y presencia de verde (mayoria del terreno). Un pantallazo vacio o de Workbench
# (azul uniforme) no pasa.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --min-white 0 --min-dark 0 --min-nonblue 20000 --need-green
