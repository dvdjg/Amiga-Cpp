#!/usr/bin/env bash
# Analisis visual de la demo 203 (polygon_planes): solido flat-shaded por bitplane.
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec node "$ROOT/tools/analyze/verify-203-polygon-planes.mjs" --image "$1"
