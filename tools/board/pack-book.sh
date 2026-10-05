#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# pack-book.sh: empaqueta un libro de aperturas (texto) en el blob binario del
# engine, listo para servir por bloques (FileBlockSource en PC / trackloader en
# Amiga). Compila `tools/board/pack_book.cpp` (usa el engine header-only) y lo
# ejecuta.
#
# Uso: tools/board/pack-book.sh <entrada.txt> [salida.bin]
#   salida por defecto: out/assets/board/book.bin
#
# Formato de entrada (una línea por jugada):
#   FEN ; uci ; score ; nombre-opcional
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CXX="${CXX:-g++}"
INPUT="${1:?uso: pack-book.sh <entrada.txt> [salida.bin]}"
OUTPUT="${2:-$ROOT/out/assets/board/book.bin}"

# Runtime del compilador en el PATH + `.exe` de MinGW (ver tools/scripts/host-cxx-env.sh).
. "$ROOT/tools/scripts/host-cxx-env.sh"
host_cxx_prepare "$CXX"

mkdir -p "$ROOT/out/tmp" "$(dirname "$OUTPUT")"
BIN="$ROOT/out/tmp/pack_book"

"$CXX" -std=gnu++23 -I"$ROOT/engine/include" -O2 "$ROOT/tools/board/pack_book.cpp" -o "$BIN"
"$(host_exe "$BIN")" "$INPUT" "$OUTPUT"
