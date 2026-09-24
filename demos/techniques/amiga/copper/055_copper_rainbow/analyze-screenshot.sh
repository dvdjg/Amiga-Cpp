#!/usr/bin/env bash
# Analizador visual de la demo 055 (copper rainbow: degradado arcoíris vía
# CopperIntent). Valida que aparezcan varias familias de tono.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi
exec node "$ROOT/dist/tools/analyze/verify_copper_rainbow.js" --image "$IMAGE"
