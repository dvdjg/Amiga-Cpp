#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate de la demo 306 (pantalla CRPG: verbos + inventario RPG + tooltip).
#
# Doble evidencia (leccion DT-035): la captura por paso de este entorno puede
# servir **video congelado**, asi que:
#   1. **Memoria** (contadores del periferico tras una ventana larga): `fps10`
#      (cadencia), `aframe` (el guion avanzo: recorre y activa) y `state`
#      (verbo/seleccion vigentes). Un `aframe` alto prueba la interaccion sin
#      depender del video.
#   2. **Visual** (secuencia por paso): presencia de texto, amarillo (verbo
#      activo), rojo (foco) y linea de estado en la franja; los chequeos de
#      animacion del verificador son informativos (el video puede congelarse).
#
# Uso: demos/features/ui/amiga/306_hud_crpg/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/306_hud_crpg"

# --- 1) Evidencia de memoria: ventana larga + contadores --------------------
COUNTERS_OUT="$(bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 8000 \
	--read-debugperiph counters 2>&1)"
echo "$COUNTERS_OUT" | grep -E 'name="(fps10|aframe|state)"' || true
FPS10="$(echo "$COUNTERS_OUT" | grep 'name="fps10"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
AFRAME="$(echo "$COUNTERS_OUT" | grep 'name="aframe"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
FPS10="${FPS10:-0}"
AFRAME="${AFRAME:-0}"
if [ "$FPS10" -lt 400 ]; then
	echo "FAIL: cadencia baja (fps10=$FPS10 < 400)" >&2
	exit 1
fi
if [ "$AFRAME" -lt 50 ]; then
	echo "FAIL: el guion no avanzo (aframe=$AFRAME < 50)" >&2
	exit 1
fi
echo "[analyze] memoria OK: fps10=$FPS10, aframe=$AFRAME (guion activo)"

# --- 2) Evidencia visual: secuencia por INTERVALOS --------------------------
# El modo `--sequence-step-frames` (probe + CPU parada por frame) sirve video
# CONGELADO en este entorno (familia DT-023/035): la captura por intervalos corre
# con la CPU viva y el video se refresca (verificado: cada par difiere y la linea
# de estado cambia). 12 frames cada 700 ms cubren varios movimientos del guion
# (16 frames de cadencia) y varios disparos (48).
RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 12 --sequence-interval-ms 700 \
	--settle-ms 3000)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run" -maxdepth 3 -type d -name sequence -path '*306_hud_crpg*' 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 306" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-hud-crpg.mjs" --sequence-dir "$SEQ"
