#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Comprueba una captura de pantalla de una demo (overlay de depuracion).
# Sustituye a analyze-screenshot.ps1. Delega en analyze_demo_screenshot.js
# (Node/pngjs) exigiendo el texto verde/amarillo/blanco del overlay.
# Uso: tools/analyze/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" --image "$IMAGE" --min-dark 0 --need-green --need-yellow
