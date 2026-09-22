#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion temporal de la demo 210_copper_blitter.
#
# Captura frames CONSECUTIVOS (1 frame entre capturas) con `--sequence-step-frames`:
# el runner congela la CPU en el *ready probe* (que la demo llama una vez por frame),
# de modo que el run status y la imagen quedan en el mismo frame (sin la latencia del
# polling del canal lateral). Luego el pixel-contract comprueba que el contenido se
# desplaza exactamente **1 px logico** (2 px de imagen, escala 2x) por frame con
# `shifted_region_match`. El inicio se alinea a `fine = 2` para evitar el cruce de
# word (wrap) de cada 16 frames, cuya actualizacion de columna ensucia el par.
#
# Uso: analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEMO="demos/amiga/210_copper_blitter"
RUN="$ROOT/tools/run/run-demo.sh"
PIXEL_ASSERT="$ROOT/tools/analyze/assert-pixel-contract.sh"
PIXEL_CONTRACT="$(dirname "${BASH_SOURCE[0]}")/pixel-contract.json"
SEQ_DIR="$ROOT/out/run/210_copper_blitter/A500_debug/sequence"
RUN_REPORT="$ROOT/out/run/210_copper_blitter/A500_debug/run-report.json"
PIXEL_OUT="$ROOT/out/analysis/210_copper_blitter/pixel-assert"

WARP=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--warp) WARP=1; shift ;;
		# El pixel assert es el nucleo de esta secuencia y se ejecuta siempre; se
		# aceptan los flags del runner de regresion para no fallar por "desconocido".
		--pixel-assert) shift ;;
		--require-pixel-assert-ok) shift ;;
		--vision-review) shift ;;
		--require-vision-review-ok) shift ;;
		--vision-provider) shift 2 ;;
		--vision-send-mode) shift 2 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

run_capture() {
	for attempt in 1 2; do
		if "$RUN" "$DEMO" --settle-ms 1200 \
			--sequence-step-frames 14 --sequence-step-start-fine 2 "${extra[@]}"; then
			return 0
		fi
		[ "$attempt" -eq 1 ] && sleep 2
	done
	echo "No se pudo capturar la secuencia step de 210_copper_blitter." >&2
	exit 1
}

run_capture

"$PIXEL_ASSERT" --sequence-dir "$SEQ_DIR" --contract "$PIXEL_CONTRACT" \
	--run-report "$RUN_REPORT" --out-dir "$PIXEL_OUT" \
	|| { echo "El scroll fino de 210_copper_blitter no es de 1 px/frame." >&2; exit 1; }

echo "OK 210_copper_blitter sequence"
