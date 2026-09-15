#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Hook prebuild de la demo 202 (lo invoca tools/build/build-demo.sh, cwd = raiz).
#
# Genera los assets DPF que la demo incbina (`const_202.h`, el banco X-Limited
# del FG y el del BG) con el pipeline de `tools/amiga-tiles` + `tools/demo202`,
# documentado en `demos/amiga/202_xlimited_dpf/README.md` ("Pipeline de assets").
#
# Se ejecuta solo si falta el resultado final, para no re-cuantizar en cada
# build; para forzarlo, borra out/assets/demo202/const_202.h.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
OUT="$ROOT/out/assets/demo202"
CONST="$OUT/const_202.h"

if [ -f "$CONST" ]; then
	exit 0
fi

PNG="$ROOT/assets/amiga/tiles-reference/Beginning Fields.png"
mkdir -p "$OUT/bg"
node "$ROOT/tools/amiga-tiles/amiga-tiles.mjs" "$PNG" \
	--colors 8 --xlimited --palette mediancut --dither atkinson --tile 16 --out "$OUT/bg"
node "$ROOT/tools/demo202/emit-202.mjs"
