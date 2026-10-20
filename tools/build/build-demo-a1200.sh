#!/usr/bin/env bash
# Build A1200/68020 nativo (A1200 con Fast RAM/accelerador). Conserva `ENGINE_OPT=-O0`
# como perfil seguro: se había reportado un cuelgue con engine `-O1` (GCC 15 m68k,
# state=1/frame=0), aunque las re-pruebas limpias actuales con `-O1` sí llegan a READY;
# el engine `-O2` aún se cuelga con las variantes ensayadas. Mantener el preset hasta
# aislar la discrepancia. El TU de la demo conserva `DEMO_OPT` de `build.args` (-O1).
#
# A1200 STOCK usa el binario 68000 `A500_release` (49,68 fps), no este build:
#   ./tools/run/run-demo-a1200-stock.sh dk_port_xl
#
# Uso:
#   ./tools/build/build-demo-a1200.sh demos/nes/dk_port_xl --release --clean
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

export AMIGA_BIN_PATH="${AMIGA_BIN_PATH:-C:/Users/dvdjg/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/win32}"
export TARGET_MACHINE="${TARGET_MACHINE:-A1200}"
export ENGINE_OPT="${ENGINE_OPT:--O0}"

exec "$ROOT/tools/build/build-demo.sh" "$@"
