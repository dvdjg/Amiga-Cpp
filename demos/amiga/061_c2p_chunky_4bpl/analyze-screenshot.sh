#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador visual de la demo 061 (rotozoom chunky 4bpp -> planar).
# Valida que la captura es COLORIDA y llena la pantalla (colores != grises, buena
# cobertura), que es lo que distingue un c2p correcto de un render plano/negro.
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