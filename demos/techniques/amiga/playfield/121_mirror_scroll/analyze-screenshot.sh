#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la captura de la demo 121 (espejo vertical con art del atlas).
# Sin overlay: no-negro + vegetación verde real del atlas.
# Uso: analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" \
	--image "$1" --min-white 0 --min-dark 0 --need-green
