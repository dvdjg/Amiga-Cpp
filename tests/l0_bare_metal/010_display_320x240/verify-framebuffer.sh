#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificador determinista del test L0-010 display_320x240.
# 1) compila, 2) lanza run-demo, 3) lee g_test_contract por el canal lateral y
# verifica las lineas del framebuffer, 4) escribe figuras por poke, 5) captura
# el framebuffer, 6) espera a que la demo restaure el sistema y captura el
# Workbench. Sustituye al antiguo verify-framebuffer.mjs.
# Uso: tools/../tests/l0_bare_metal/010_display_320x240/verify-framebuffer.sh
#      [--demo <ruta>] [--skip-build] [--wait-ms N] ...
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

exec node "$ROOT/dist/tests/l0_bare_metal/010_display_320x240/verify-framebuffer.js" "$@"
