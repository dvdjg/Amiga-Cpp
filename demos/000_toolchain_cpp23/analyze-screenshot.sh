#!/usr/bin/env bash
# Comprueba la captura de pantalla de esta demo (helper multiplataforma).
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/tools/analyze/analyze_demo_screenshot.py" --image "$1" --demo "000_toolchain_cpp23" --min-white 40 --min-nonblue 400 --min-dark 20
