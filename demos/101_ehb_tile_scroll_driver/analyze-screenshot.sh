#!/usr/bin/env bash
# Comprueba la captura de pantalla de esta demo (helper multiplataforma).
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/tools/analyze/analyze_demo_screenshot.py" --image "$1" --demo "101_ehb_tile_scroll_driver" 
