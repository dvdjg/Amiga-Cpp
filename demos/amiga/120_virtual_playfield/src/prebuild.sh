#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Hook prebuild de la demo 120 (lo invoca tools/build/build-demo.sh, cwd = raiz).
#
# Genera el banco X-Limited a 8 colores (3 planos interleaved) que la demo
# incbina, desde el atlas "Beginning Fields" con `tools/amiga-tiles`.
# Se ejecuta solo si falta el resultado, para no re-cuantizar en cada build.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
OUT="$ROOT/out/assets/beginning-fields/8c"
BANK="$OUT/tilebank_xlimited_8c_t16_mediancut_none.bin"

if [ -f "$BANK" ]; then
	exit 0
fi

node "$ROOT/tools/amiga-tiles/amiga-tiles.mjs" \
	"$ROOT/assets/amiga/tiles-reference/Beginning Fields.png" \
	--colors 8 --palette mediancut --dither none --tile 16 --xlimited --out "$OUT"
