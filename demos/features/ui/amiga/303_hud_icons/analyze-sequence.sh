#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate de la demo 303 (inventario con seleccion y foco).
#
# Doble evidencia (leccion DT-035):
#   1. **Memoria** (contadores del periferico tras una ventana larga): el ratio
#      `frame`/`vbl` mide la **perdida real de campos** contra el reloj de VBlank
#      del mini-SO (50 Hz exactos) — el `fps10` del periferico usa el reloj de
#      pared del emulador y subestima (DT-036). Con el bucle por **sondeo**
#      (`run_polling`, familia 212/DT-011) el ratio es ~1,0; con los bucles por
#      IRQ bajo SO vivo la demo perdia latidos (~0,88) o moria (paron).
#   2. **Visual** (secuencia por intervalos): franja con texto, objetos de la
#      rejilla y marco del foco, y la celda seleccionada CAMBIA entre frames.
#
# Uso: demos/features/ui/amiga/303_hud_icons/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/303_hud_icons"

# --- 1) Evidencia de memoria: ventana larga + ratio frame/vbl -----------------
COUNTERS_OUT="$(bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 15000 \
	--read-debugperiph counters 2>&1)"
echo "$COUNTERS_OUT" | grep -E 'name="(fps10|vbl|frame)"' || true
VBL="$(echo "$COUNTERS_OUT" | grep 'name="vbl"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
FRAME="$(echo "$COUNTERS_OUT" | grep 'name="frame"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
VBL="${VBL:-0}"
FRAME="${FRAME:-0}"
if [ "$VBL" -lt 500 ]; then
	echo "FAIL: el latido de VBlank no avanzo (vbl=$VBL; app muerta o ventana corta)" >&2
	exit 1
fi
# Ratio en mil: frame*1000/vbl >= 950 (tolera el primer frame y el redondeo).
RATIO=$((FRAME * 1000 / VBL))
if [ "$RATIO" -lt 950 ]; then
	echo "FAIL: perdida de campos (frame=$FRAME / vbl=$VBL = $RATIO/1000 < 950)" >&2
	exit 1
fi
echo "[analyze] memoria OK: vbl=$VBL, frame=$FRAME (ratio $RATIO/1000)"

# --- 2) Evidencia visual: secuencia por INTERVALOS ---------------------------
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
