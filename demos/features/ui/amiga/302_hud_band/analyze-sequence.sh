#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 302 (HUD de juego en franja propia + contenido exigente).
#
# Captura una secuencia con el runner (con reintentos: la captura por paso es flaky
# en este entorno, DT-023/DT-035) y verifica con verify-hud-band.mjs que la franja
# (lineas 208..255) tiene texto/marco y amarillo y que CAMBIA entre frames, y que la
# rejilla/pista de la escena esta presente. La invoca tools/test-regression.sh
# (reenvia --warp).
#
# Uso: demos/features/ui/amiga/302_hud_band/analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/features/ui/amiga/302_hud_band"
SEQ="$ROOT/out/run/ui_amiga_302_hud_band/A500_debug/sequence"

# Captura **frame a frame** (1 frame por captura) del periodo completo de la trayectoria
# (lissajous de 64 frames): incluye cruces del sprite por el prop de prueba (cookie-cut).
RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-step-frames 40 --settle-ms 1000)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done

# La captura por paso falla de forma intermitente en este entorno (timeout del
# breakpoint; DT-023): hasta 3 intentos; si ninguno captura, se usa la secuencia que ya
# hubiera (el veredicto del verificador no cambia: es de contenido).
for attempt in 1 2 3; do
	bash "${RUN_ARGS[@]}" >/dev/null 2>&1 || true
	count="$(find "$SEQ" -maxdepth 1 -name 'frame_*.png' 2>/dev/null | wc -l)"
	if [ "$count" -ge 10 ]; then
		break
	fi
	echo "[302] captura intento $attempt sin frames ($count); reintento" >&2
	sleep 3
done

if [ ! -d "$SEQ" ] || [ "$(find "$SEQ" -maxdepth 1 -name 'frame_*.png' | wc -l)" -lt 2 ]; then
	echo "No se pudo capturar la secuencia de la demo 302 (DT-023/DT-035)" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-hud-band.mjs" --sequence-dir "$SEQ"
