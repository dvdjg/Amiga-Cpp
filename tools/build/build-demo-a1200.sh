#!/usr/bin/env bash
# Build A1200/68020 nativo. Fija `ENGINE_OPT=-O0` por el bug de codegen de
# GCC 15 m68k a -O1: el engine compilado a -O1 cuelga el arranque del 68020
# antes de `mark_init_started` (state=1/frame=0). El TU de la demo conserva
# `DEMO_OPT` de `build.args` (-O1), que es el codegen estable del port.
#
# Uso:
#   ./tools/build/build-demo-a1200.sh demos/nes/dk_port_xl --release --clean
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

export AMIGA_BIN_PATH="${AMIGA_BIN_PATH:-C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32}"
export TARGET_MACHINE="${TARGET_MACHINE:-A1200}"
export ENGINE_OPT="${ENGINE_OPT:--O0}"

exec "$ROOT/tools/build/build-demo.sh" "$@"
