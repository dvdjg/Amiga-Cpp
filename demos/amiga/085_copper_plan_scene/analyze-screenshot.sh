#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador visual de la demo 085 (escena con copper orquestado por CopperPlan).
# Valida que la captura es COLORIDA y llena la pantalla: el cielo por bandas de
# COLOR00 (degradado) + el BOB con su propio degradado anclado a su Y.
# Uso: analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/verify_c2p_color.js" --image "$IMAGE"
