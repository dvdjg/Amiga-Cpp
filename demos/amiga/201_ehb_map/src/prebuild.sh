#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Hook prebuild de la demo 201 (lo invoca tools/build/build-demo.sh, cwd = raiz).
#
# Genera los assets EHB que la demo incbina (`const_game_201.h` y
# `tilebank.xlimited.bin`) con el pipeline de `tools/ehb`, documentado en
# `demos/amiga/201_ehb_map/src/README.md` §5.
#
# Se ejecuta solo si falta el resultado final, para no re-cuantizar en cada
# build; para forzarlo, borra out/assets/ehb/const_game_201.h.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
OUT="$ROOT/out/assets/ehb"
CONST="$OUT/const_game_201.h"

if [ -f "$CONST" ]; then
	exit 0
fi

PNG="$ROOT/assets/amiga/tiles-reference/Beginning Fields.png"
mkdir -p "$OUT"
node "$ROOT/tools/ehb/quantize-ehb.mjs" "$PNG" --out "$OUT"
node "$ROOT/tools/ehb/slice-tiles.mjs" "$PNG" --palette "$OUT/palette.json" --out "$OUT"
node "$ROOT/tools/ehb/emit-const-201.mjs" --in "$OUT/tilebank_indexed.h" --json "$OUT/tiles.json" --out "$CONST"
node "$ROOT/tools/ehb/emit-xlimited-bank.mjs" --in "$OUT/tilebank.raw.bin" --out-bin "$OUT/tilebank.xlimited.bin" --out-h "$OUT/tilebank.xlimited.h"
