#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Valida la transicion de fine scroll horizontal (BPLCON1) de la demo 101:
# captura 4 frames CONSECUTIVOS con `--sequence-step-frames 4 --sequence-step-start-fine 14`
# (fine 14,15,0,1 = cruce de word) y comprueba con analyze_fine_scroll.js que el borde
# izquierdo no salta. El step capture congela la CPU en el ready probe, por lo que
# captura la transicion completa sin la latencia del polling de `--sequence-camera-x`.
# Uso: analyze-fine-scroll.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
RUN="$ROOT/tools/run/run-demo.sh"
PY="$ROOT/dist/demos/amiga/101_ehb_tile_scroll_driver/analyze_fine_scroll.js"
SEQ_DIR="$ROOT/out/run/101_ehb_tile_scroll_driver/A500_debug/sequence"
RUN_REPORT="$ROOT/out/run/101_ehb_tile_scroll_driver/A500_debug/run-report.json"

WARP=0
for arg in "$@"; do
	[ "$arg" = "--warp" ] && WARP=1
done

extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)
if ! "$RUN" demos/amiga/101_ehb_tile_scroll_driver --settle-ms 0 \
	--sequence-step-frames 4 --sequence-step-start-fine 14 "${extra[@]}"; then
	echo "No se pudo capturar la transicion fine scroll (start-fine=14)." >&2
	exit 1
fi
# cameraX 14,15,16,17 -> fine 14,15,0,1: desplazamiento continuo de 2 px PNG (1 px logico)
# por paso (positivo = contenido desplazado a la izquierda).
if ! node "$PY" "$SEQ_DIR" "$RUN_REPORT" "14,15,16,17" "2,2,2"; then
	echo "La transicion fine scroll no es continua." >&2
	exit 1
fi
echo "OK fine scroll"
