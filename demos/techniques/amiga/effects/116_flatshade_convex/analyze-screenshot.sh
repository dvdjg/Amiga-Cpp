#!/usr/bin/env bash
# Analisis visual de la demo 116 (flatshade-convex): balon convexo flat-shaded.
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
exec node "$ROOT/tools/analyze/verify-116-flatshade.mjs" "$1"
