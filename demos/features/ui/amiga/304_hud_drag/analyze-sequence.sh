#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate de la demo 304 (inventario RPG con arrastre: fantasma, drop y cancelacion).
#
# Doble evidencia (leccion DT-035):
#   1. **Memoria** (contadores tras ventana larga): `fps10` (cadencia) y
#      `drag_ok`/`drag_bad` (ciclos de arrastre completados; bad debe ser 0).
#   2. **Visual** (secuencia por intervalos, CPU viva): presencia de texto,
#      amarillo (estado) y rojo (origen/destino/fantasma) en la franja; los
#      chequeos de animacion del verificador son informativos (el video puede
#      congelarse en este entorno: DT-023/035).
#
# Uso: demos/features/ui/amiga/304_hud_drag/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/304_hud_drag"

# --- 1) Evidencia de memoria: ventana larga + contadores --------------------
COUNTERS_OUT="$(bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 12000 \
	--read-debugperiph counters 2>&1)"
echo "$COUNTERS_OUT" | grep -E 'name="(fps10|drag_ok|drag_bad)"' || true
FPS10="$(echo "$COUNTERS_OUT" | grep 'name="fps10"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
DRAG_OK="$(echo "$COUNTERS_OUT" | grep 'name="drag_ok"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
DRAG_BAD="$(echo "$COUNTERS_OUT" | grep 'name="drag_bad"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
FPS10="${FPS10:-0}"
DRAG_OK="${DRAG_OK:-0}"
DRAG_BAD="${DRAG_BAD:-0}"
if [ "$FPS10" -lt 400 ]; then
	echo "FAIL: cadencia baja (fps10=$FPS10 < 400)" >&2
	exit 1
fi
if [ "$DRAG_OK" -lt 2 ]; then
	echo "FAIL: menos de 2 arrastres completados (drag_ok=$DRAG_OK)" >&2
	exit 1
fi
if [ "$DRAG_BAD" -ne 0 ]; then
	echo "FAIL: arrastres invalidos (drag_bad=$DRAG_BAD)" >&2
	exit 1
fi
echo "[analyze] memoria OK: fps10=$FPS10, drag_ok=$DRAG_OK, drag_bad=$DRAG_BAD"

# --- 2) Evidencia visual: secuencia por intervalos --------------------------
RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 8 --sequence-interval-ms 1200 \
	--settle-ms 2500)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*304_hud_drag*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 304" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-hud-drag.mjs" --sequence-dir "$SEQ"
