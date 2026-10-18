#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 302 (HUD de juego en franja propia).
#
# Captura una secuencia con el runner y verifica que la franja (lineas 208..255)
# tiene texto/marco y relleno amarillo, y que CAMBIA entre frames consecutivos
# (contador y energia). La invoca tools/test-regression.sh (reenvia --warp).
#
# Uso: demos/features/ui/amiga/302_hud_band/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/302_hud_band"

# Captura **frame a frame** (1 frame por captura) con asentamiento corto: la ventana
# incluye el CRUCE del actor por el bloque de color (f~12..24), donde se verifica el
# cookie-cut (sin banda negra dentro del bloque). La secuencia es determinista y el
# detector de flicker ve movimiento pequeno y coherente (con capturas separadas 200 ms,
# el actor y la barra parecen "parpadeo").
RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-step-frames 20 --settle-ms 150)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*302_hud_band*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 302" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-hud-band.mjs" --sequence-dir "$SEQ"
