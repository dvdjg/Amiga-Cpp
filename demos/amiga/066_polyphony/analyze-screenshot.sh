#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador propio de la demo 066_polyphony.
#
# Es una demo de AUDIO: no dibuja nada (pantalla negra por diseno) y el analizador
# generico exige texto blanco de un overlay que no existe. Se valida que la captura
# tiene tamano de display, que el run-report es de esta demo y que llego a Ready
# (state 3); no se exige blanco (min-white 0). El fondo negro puntua como "dark".
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
	--image "$IMAGE" --demo 066_polyphony --min-white 0 --min-dark 0
