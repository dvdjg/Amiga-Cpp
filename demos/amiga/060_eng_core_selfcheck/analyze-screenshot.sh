#!/usr/bin/env bash
# Comprueba la captura de pantalla de esta demo (helper multiplataforma).
# Uso: analyze-screenshot.sh <imagen.png>
# La demo dibuja en bitplanes EHB (320x256): fondo azul oscuro + texto blanco
# con la fuente 8x8. Se validan white (texto) y nonblue (fondo distinto del
# Workbench), sin exigir verde.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/analyze_demo_screenshot.js" --image "$1" --demo "060_eng_core_selfcheck" --min-white 10 --min-nonblue 120 --min-dark 5