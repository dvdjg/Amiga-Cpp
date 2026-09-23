#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificador determinista del test L0-020 math_scalars.
# Compila el test, lo ejecuta en WinUAE y lee `g_math_report` por el canal lateral
# para exigir que TODOS los casos (MF/q12/q8/q0 + operaciones entre tipos) pasen.
# Uso: tests/l0_bare_metal/020_math_scalars/verify-math.sh
#      [--demo <ruta>] [--skip-build] [--port N] [--wait-ms N] [--verbose] [--keep]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"

exec node "$ROOT/dist/tests/l0_bare_metal/020_math_scalars/verify-math.js" "$@"
