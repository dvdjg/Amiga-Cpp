#!/usr/bin/env bash
# Comprueba la captura de pantalla de esta demo (helper multiplataforma).
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" --image "$1" --demo "000_toolchain_cpp23" --min-white 40 --min-nonblue 400 --min-dark 20
