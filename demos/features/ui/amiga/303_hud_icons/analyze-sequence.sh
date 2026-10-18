#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 303 (inventario con seleccion y foco).
#
# Captura una secuencia con el runner y verifica que la franja (lineas 208..255)
# tiene texto, los objetos de la rejilla y el marco del foco, y que la **celda
# seleccionada CAMBIA** entre frames (la interaccion avanza con el guion o el
# input). La invoca tools/test-regression.sh (reenvia --warp).
#
# Uso: demos/features/ui/amiga/303_hud_icons/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/303_hud_icons"

# Captura por **intervalos** (CPU viva): el modo `--sequence-step-frames` sirve video
# congelado en este entorno (DT-035); 12 frames cada 700 ms cubren el guion (paso cada 12).
RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 12 --sequence-interval-ms 700 --settle-ms 3000)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*303_hud_icons*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 303" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-hud-icons.mjs" --sequence-dir "$SEQ"
