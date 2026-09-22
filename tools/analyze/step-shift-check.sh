#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion reutilizable de scroll por frame: captura N frames CONSECUTIVOS con
# `--sequence-step-frames` (run-demo congela la CPU en el *ready probe*) y comprueba
# con `shifted_region_match` que el contenido se desplaza un offset fijo por frame
# (p. ej. 1 px logico por frame de scroll fino).
#
# Encapsula los flags y las rutas (que incluyen el CONFIG_ID) para que cada demo no
# los duplique. El contrato declara el `dx` esperado y la ROI.
#
# Uso: step-shift-check.sh --demo <ruta> --contract <pixel-contract.json>
#      [--config A500_debug] [--frames 14] [--start-fine 2] [--settle-ms 800] [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

DEMO=""; CONTRACT=""; CONFIG="A500_debug"; FRAMES=14; START_FINE=2; SETTLE=800; WARP=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--demo) DEMO="$2"; shift 2 ;;
		--contract) CONTRACT="$2"; shift 2 ;;
		--config) CONFIG="$2"; shift 2 ;;
		--frames) FRAMES="$2"; shift 2 ;;
		--start-fine) START_FINE="$2"; shift 2 ;;
		--settle-ms) SETTLE="$2"; shift 2 ;;
		--warp) WARP=1; shift ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done
if [ -z "$DEMO" ] || [ -z "$CONTRACT" ]; then
	echo "Uso: step-shift-check.sh --demo <ruta> --contract <pixel-contract.json> [opciones]" >&2
	exit 2
fi

DEMO_NAME="$(basename "$DEMO")"
SEQ_DIR="$ROOT/out/run/$DEMO_NAME/$CONFIG/sequence"
RUN_REPORT="$ROOT/out/run/$DEMO_NAME/$CONFIG/run-report.json"
PIXEL_OUT="$ROOT/out/analysis/$DEMO_NAME/pixel-assert"

RUN="$ROOT/tools/run/run-demo.sh"
PIXEL_ASSERT="$ROOT/tools/analyze/assert-pixel-contract.sh"

extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

"$RUN" "$DEMO" --config "$CONFIG" --settle-ms "$SETTLE" \
	--sequence-step-frames "$FRAMES" --sequence-step-start-fine "$START_FINE" "${extra[@]}" \
	|| { echo "No se pudo capturar la secuencia step de $DEMO_NAME." >&2; exit 1; }

"$PIXEL_ASSERT" --sequence-dir "$SEQ_DIR" --contract "$CONTRACT" \
	--run-report "$RUN_REPORT" --out-dir "$PIXEL_OUT" \
	|| { echo "El desplazamiento por frame de $DEMO_NAME no coincide con el contrato." >&2; exit 1; }

echo "OK $DEMO_NAME step-shift"
