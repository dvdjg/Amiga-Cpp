#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# selfplay.sh: compila y ejecuta partidas de poker CPU vs CPU en host con la
# MISMA configuracion que usara el juego (estilos, perfil de memoria, tabla
# preflop y rango de rival), sin UI.
#
# Uso: tools/cards/selfplay.sh [hands] [--seats N] [--seed S] [--stack N]
#        [--sb N] [--bb N] [--profile N20|N64|N128|N256|N512] [--sessions N]
#        [--table-samples N] [--range-classes N] [--range-mode dynamic|table|none]
#        [--styles tp,ta,lp,la,eq] [--compare] [--sweep] [--csv ruta.csv]
#        [--out ruta] [--quiet]
#   por defecto: 200 --seats 6 --seed 1 --profile N512 --range-mode dynamic
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
BIN="$ROOT/out/tmp/cards-selfplay"

mkdir -p "$ROOT/out/tmp"
"$CXX" -std=gnu++23 -I"$ROOT/engine/include" -O2 -Wall -Wextra "$ROOT/tools/cards/selfplay.cpp" -o "$BIN"
"$BIN" "$@"
