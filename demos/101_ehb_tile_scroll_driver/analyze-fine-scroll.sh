#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Valida las transiciones de fine scroll horizontal (BPLCON1) de la demo 101.
# Sustituye a analyze-fine-scroll.ps1. Captura con run-demo --sequence-camera-x
# y valida con analyze_fine_scroll.js (pngjs).
# Uso: analyze-fine-scroll.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUN="$ROOT/tools/run/run-demo.sh"
PY="$ROOT/dist/demos/101_ehb_tile_scroll_driver/analyze_fine_scroll.js"
SEQ_DIR="$ROOT/out/run/101_ehb_tile_scroll_driver/sequence"
RUN_REPORT="$ROOT/out/run/101_ehb_tile_scroll_driver/run-report.json"

WARP=0
for arg in "$@"; do
	[ "$arg" = "--warp" ] && WARP=1
done

invoke_case() {
	local camera_x="$1" expected_shifts="$2"
	local extra=()
	[ "$WARP" -eq 1 ] && extra+=(--warp)
	if ! "$RUN" demos/101_ehb_tile_scroll_driver --settle-ms 0 --sequence-camera-x "$camera_x" "${extra[@]}"; then
		echo "No se pudo capturar la transicion fine scroll cameraX=$camera_x." >&2
		exit 1
	fi
	if ! node "$PY" "$SEQ_DIR" "$RUN_REPORT" "$camera_x" "$expected_shifts"; then
		echo "La transicion fine scroll no es continua." >&2
		exit 1
	fi
}

invoke_case "94,95,96,97" "-2,-2,-2"
invoke_case "112,111,110,109" "2,2,2"
echo "OK fine scroll"
