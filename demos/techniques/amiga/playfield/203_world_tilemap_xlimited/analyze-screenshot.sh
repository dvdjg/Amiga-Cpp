#!/usr/bin/env bash
# Analizador de la escena plana sin rótulo de depuración; exige cobertura de contenido.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then echo "Uso: analyze-screenshot.sh <imagen.png>" >&2; exit 2; fi
exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$IMAGE" --min-white 0 --min-dark 0 --min-nonblue 10000
