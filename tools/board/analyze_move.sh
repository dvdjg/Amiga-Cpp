#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze_move.sh: reanaliza una posicion y explica por que se eligio una jugada.
#
# Uso:
#   tools/board/analyze_move.sh --fen "rnbqkbnr/... w KQkq - 0 1" [--style ...]
#   tools/board/analyze_move.sh --dump out/board/selfplay/pos.txt --ply N [--depth D]
#
# El volcado lo genera `tools/board/selfplay.sh ... --dump-positions <file>`.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
BIN="$ROOT/out/tmp/analyze_move"

mkdir -p "$ROOT/out/tmp"
"$CXX" -std=gnu++23 -I"$ROOT/engine/include" -O2 -Wall -Wextra "$ROOT/tools/board/analyze_move.cpp" -o "$BIN"
"$BIN" "$@"
