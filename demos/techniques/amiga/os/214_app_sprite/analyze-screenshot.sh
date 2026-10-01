#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual determinista de demo 214: la paleta requiere el centro amarillo
# de los sprites. Evita aceptar como correcta una captura que solo tenga fondo.
# Invocado por tools/analyze/analyze-demo.sh.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --min-white 0 --min-nonblue 100 --need-yellow
