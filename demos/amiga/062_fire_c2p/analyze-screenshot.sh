#!/usr/bin/env bash
# Analizador visual de la demo 062 (fuego + c2p). Valida pixeles calientes en la
# mitad inferior con degradado (no un color plano).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi
exec node "$ROOT/dist/tools/analyze/verify_fire.js" --image "$IMAGE"