#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador de la demo 084_mf_rotation (cubo 3D con rotacion en MiniFloat16).
#
# No hay overlay: el cubo se traza en una rampa azul->cian->dorado->blanco sobre
# fondo azul oscuro, con marco y estrellas. Exige contenido (no-azul-Workbench) y
# presencia de blanco (las aristas del cubo mas cercanas). El amarillo no se exige:
# depende de la orientacion de la captura.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --min-white 8 --min-dark 0 --min-nonblue 5000
