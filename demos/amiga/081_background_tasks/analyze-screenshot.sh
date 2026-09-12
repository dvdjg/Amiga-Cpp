#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador propio de la demo 081_background_tasks.
#
# La demo no dibuja el overlay de depuracion: muestra una barra blanca (progreso de
# la tarea de fondo) sobre un fondo que el bucle principal pulsa. Exige blanco (la
# barra) y fondo oscuro, sin pedir verde/amarillo.
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
	--image "$IMAGE" --min-white 10 --min-dark 10
