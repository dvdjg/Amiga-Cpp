#!/usr/bin/env bash
# Analizador visual de la demo 053 (multiplexado de sprites + color multiplexing).
# Valida seis tonos saturados en seis franjas verticales ordenadas.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi
exec node "$ROOT/dist/tools/analyze/verify_sprite_multiplex.js" --image "$IMAGE"
