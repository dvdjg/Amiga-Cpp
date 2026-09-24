#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador propio de la demo 086_bob_objects.
#
# La demo no dibuja el overlay de depuracion: muestra BOBs con un arcoiris de
# 4 pasos por filas (Copper anclado al objeto) sobre un degradado de cielo. Por
# eso no se exige texto blanco (el analizador generico pide 10 muestras blancas
# que esta paleta no tiene) y en su lugar se exige verde y amarillo, presentes
# en el arcoiris de cada objeto, y contenido no-azul-Workbench (el degradado).
#
# Tambien valida que el run-report.json adjunto es de esta demo y llego a Ready.
#
# Uso: tools/analyze/analyze-screenshot.sh <imagen.png> (invocado por analyze-demo.sh)
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --demo 086_bob_objects \
	--min-white 0 --min-dark 0 --need-green --need-yellow
