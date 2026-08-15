#!/usr/bin/env bash
# Comprueba la captura de pantalla de esta demo (helper multiplataforma).
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" --image "$1" --demo "100_virtual_tile_scene_scroll" 
