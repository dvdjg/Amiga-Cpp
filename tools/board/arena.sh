#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# arena.sh: compila y ejecuta un torneo rápido de ajedrez (motor vs motor) con
# variantes (standard/chess960). Ver tools/board/arena.cpp.
#
# Uso: tools/board/arena.sh [games] [depth] [variant] [seed] [max_plies] [nodes]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
BIN="$ROOT/out/tmp/arena"

# Runtime del compilador en el PATH + `.exe` de MinGW (ver tools/scripts/host-cxx-env.sh).
. "$ROOT/tools/scripts/host-cxx-env.sh"
host_cxx_prepare "$CXX"

mkdir -p "$ROOT/out/tmp"
"$CXX" -std=gnu++23 -I"$ROOT/engine/include" -O2 "$ROOT/tools/board/arena.cpp" -o "$BIN"
"$(host_exe "$BIN")" "$@"
