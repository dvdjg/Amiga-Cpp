#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador de la demo 084_mf_rotation (cubo 3D con rotacion en MiniFloat16).
#
# No hay overlay: el cubo se traza en una rampa azul->cian->amarillo->blanco sobre
# fondo azul oscuro, con marco y estrellas. Exige contenido (no-azul-Workbench),
# presencia de blanco (aristas cercanas del cubo) y de amarillo (rampa calida).
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --min-white 20 --min-dark 0 --min-nonblue 5000 --need-yellow
