#!/usr/bin/env bash
# Analizador visual de la demo 063 (benchmark fuego C++ vs asm). Valida pixeles
# calientes (fuego) en la mitad inferior, como la demo 062.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi
exec node "$ROOT/dist/tools/analyze/verify_fire.js" --image "$IMAGE"