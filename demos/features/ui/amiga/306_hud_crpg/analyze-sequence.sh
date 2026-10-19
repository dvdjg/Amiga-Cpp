#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate de la demo 306 (pantalla CRPG: verbos + inventario RPG + tooltip).
#
# Doble evidencia (leccion DT-035): la captura por paso de este entorno puede
# servir **video congelado**, asi que:
#   1. **Memoria** (contadores del periferico tras una ventana larga): `fps10`
#      (cadencia), `aframe` (el guion avanzo: recorre y activa), `state`
#      (verbo/seleccion vigentes) e `iconset` (el **ciclo de tema/skin** se
#      disparo: >=1 cambio en la ventana, con la app corriendo de verdad >300
#      frames — familia 212/DT-011 resuelta con la espera activa de VBlank).
#   2. **Visual** (secuencia por intervalos): presencia de texto, amarillo
#      (verbo activo), rojo (foco) e iconos del atlas en la franja; los
#      chequeos de animacion del verificador son informativos (el video puede
#      congelarse: DT-023/035).
#
# Uso: demos/features/ui/amiga/306_hud_crpg/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/306_hud_crpg"

# --- 1) Evidencia de memoria: ventana larga + contadores --------------------
# 9 s (~450 frames a 50 fps) garantizan al menos un ciclo de tema (cada 300).
COUNTERS_OUT="$(bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 9000 \
	--read-debugperiph counters 2>&1)"
echo "$COUNTERS_OUT" | grep -E 'name="(fps10|aframe|state|iconset)"' || true
FPS10="$(echo "$COUNTERS_OUT" | grep 'name="fps10"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
AFRAME="$(echo "$COUNTERS_OUT" | grep 'name="aframe"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
STATE="$(echo "$COUNTERS_OUT" | grep 'name="state"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
ICONSET="$(echo "$COUNTERS_OUT" | grep 'name="iconset"' | grep -o 'value=[0-9]*' | cut -d= -f2)"
FPS10="${FPS10:-0}"
AFRAME="${AFRAME:-0}"
STATE="${STATE:-0}"
ICONSET="${ICONSET:-0}"
if [ "$FPS10" -lt 400 ]; then
	echo "FAIL: cadencia baja (fps10=$FPS10 < 400)" >&2
	exit 1
fi
# El guion avanza (aframe) o la entrada real movio el estado (verb*100+sel != inicial):
# cualquiera de los dos prueba la via de interaccion (el raton puede resetear aframe).
if [ "$AFRAME" -lt 50 ] && [ "$STATE" -eq 0 ]; then
	echo "FAIL: sin interaccion (aframe=$AFRAME, state=$STATE)" >&2
	exit 1
fi
# Ciclo de tema/skin: al menos un cambio en la ventana (app corriendo >300 frames).
if [ "$ICONSET" -lt 1 ]; then
	echo "FAIL: el ciclo de tema/skin no se disparo (iconset=$ICONSET; app estancada o ventana corta)" >&2
	exit 1
fi
echo "[analyze] memoria OK: fps10=$FPS10, aframe=$AFRAME, state=$STATE, iconset=$ICONSET"

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
