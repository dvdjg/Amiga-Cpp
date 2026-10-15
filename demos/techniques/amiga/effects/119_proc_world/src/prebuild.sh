#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Hook prebuild de la demo 119 (lo invoca tools/build/build-demo.sh, cwd = raiz).
#
# Cocina en el HOST el atlas y el set de terreno con la misma librería del engine
# (`eng::gfx::proc`, MiniFloat16) y deja el header que la demo incrusta en
# `out/assets/proc-world/proc_world_assets.h`.
#
# Por qué precocinado: la generación completa cuesta ~3,4 G de ciclos en un 68000
# (medido; ver VALIDATION.md), demasiado para una carga interactiva. Los assets son
# deterministas byte a byte (lo ancla HOST-443), así que host y Amiga muestran el
# MISMO mundo. Se ejecuta solo si falta el header (borrarlo para regenerar).
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../../.." && pwd)"
OUT_DIR="$ROOT/out/assets/proc-world"
HEADER="$OUT_DIR/proc_world_assets.h"
GEN_SRC="$ROOT/demos/techniques/amiga/effects/119_proc_world/tools/gen_assets.cpp"

if [ -f "$HEADER" ]; then
	exit 0
fi

mkdir -p "$OUT_DIR"
CXX="${CXX:-g++}"
"$CXX" -std=gnu++23 -O2 -I"$ROOT/engine/include" "$GEN_SRC" -o "$OUT_DIR/gen_assets"
"$OUT_DIR/gen_assets" "$HEADER"
echo "[119 prebuild] assets cocinados: $HEADER"
