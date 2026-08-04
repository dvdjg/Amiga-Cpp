#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Inyecta una trayectoria de raton emulado en WinUAE-DBG por el canal lateral.
# Sustituye al antiguo mouse-path.ps1 (multiplataforma).
#
# Uso: tools/input/mouse-path.sh <demo> [opciones]
#   --from X1,Y1 --to X2,Y2 [--steps N] [--click] [--drag] [--button N]
#   --control lineal|quadratic|cubic
# Ver docs/emulation/MOUSE_AUTOMATION.md.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

exec node "$ROOT/dist/tools/input/mouse-path.js" "$@"
