#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion temporal de la demo 210_copper_blitter.
#
# Delega en `tools/analyze/step-shift-check.sh`: captura frames CONSECUTIVOS
# (1 frame entre capturas, CPU congelada en el ready probe) y comprueba con el
# `pixel-contract.json` que el contenido se desplaza 1 px logico por frame
# (`shifted_region_match`). El inicio se alinea a fine=2 para evitar el cruce de
# word (wrap) de cada 16 frames, cuya actualizacion de columna ensucia el par.
#
# Uso: analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/blitter/210_copper_blitter"
HELPER="$ROOT/tools/analyze/step-shift-check.sh"
CONTRACT="$(dirname "${BASH_SOURCE[0]}")/pixel-contract.json"

WARP=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--warp) WARP=1; shift ;;
		# Flags del runner de regresion (el pixel assert es el nucleo y va siempre).
		--pixel-assert|--require-pixel-assert-ok|--vision-review|--require-vision-review-ok) shift ;;
		--vision-provider|--vision-send-mode) shift 2 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

"$HELPER" --demo "$DEMO" --contract "$CONTRACT" \
	--frames 14 --start-fine 2 --settle-ms 1200 "${extra[@]}" \
	|| { echo "El scroll fino de 210_copper_blitter no es de 1 px/frame." >&2; exit 1; }

echo "OK 210_copper_blitter sequence"
