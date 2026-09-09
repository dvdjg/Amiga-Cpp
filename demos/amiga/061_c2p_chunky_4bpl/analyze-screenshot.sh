#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Analizador visual de la demo 061 (c2p chunky->planar).
# Valida que la captura muestra la rampa de grises i*0x111 (indices 0..15) en la
# zona del buffer chunky (arriba-izquierda). No usa overlay verde/amarillo.
# Uso: analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/verify_c2p_gray.js" --image "$IMAGE"