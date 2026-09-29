#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# selfplay.sh: compila y ejecuta partidas completas en host con la MISMA
# configuracion que la demo demos/features/board/chess/amiga/123_chess_match (estilos, libro,
# rebanadas y relojes), sin UI. Exporta cada partida a PGN.
#
# Uso: tools/board/selfplay.sh [games] [--variant standard|chess960] [--seed N]
#        [--max-plies N] [--out ruta.pgn] [--swap] [--no-book] [--quiet]
#   por defecto: 1 standard 0 300 out/board/selfplay/selfplay.pgn
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
BIN="$ROOT/out/tmp/selfplay"

mkdir -p "$ROOT/out/tmp"
"$CXX" -std=gnu++23 -I"$ROOT/engine/include" -O2 -Wall -Wextra "$ROOT/tools/board/selfplay.cpp" -o "$BIN"
"$BIN" "$@"
