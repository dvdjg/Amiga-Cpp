#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Compila UN mismo fuente contra los tres modos del escalar generico y compara
# sus salidas:
#
#   NATIVE   (por defecto en host)      int / float
#   RETRO16  (-DENG_SCALAR_RETRO16)     s16 / Fixed<s16,12> / Fixed<s16,0>
#   RETRO32  (-DENG_SCALAR_RETRO32)     s32 / Fixed<s32,12> / Fixed<s32,0>
#
# Sirve para ver como cambia la precision de un mismo algoritmo de simulacion
# sin tocar el codigo (ver engine/include/eng/core/math/scalar.hpp). Es la evidencia
# de la fase F5 del roadmap del escalar generico.
#
# Uso: tools/run/run-scalar-modes.sh [tests/host/NNN_x/src/main.cpp]
#   Sin argumentos: tests/host/core/136_real_scalar/src/main.cpp
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${1:-$ROOT/tests/host/core/136_real_scalar/src/main.cpp}"
OUT="$ROOT/out/host-tests"
FLAGS="-std=gnu++23 -I$ROOT/engine/include -Wall -Wextra -Werror=narrowing -O2"
CXX="${CXX:-g++}"

if [ ! -f "$SRC" ]; then
	echo "ERROR: no existe el fuente '$SRC'." >&2
	exit 1
fi

mkdir -p "$OUT"

for mode in native RETRO16 RETRO32; do
	def=""
	if [ "$mode" != "native" ]; then
		def="-DENG_SCALAR_$mode"
	fi
	bin="$OUT/scalar-modes-$mode"
	echo "== $mode =="
	"$CXX" $FLAGS $def "$SRC" -o "$bin"
	"$bin"
	echo
done
