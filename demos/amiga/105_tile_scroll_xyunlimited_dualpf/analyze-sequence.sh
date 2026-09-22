#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion temporal de la demo 105 (scroll XY unlimited, dual PF).
#
# Captura frames CONSECUTIVOS con `--sequence-step-frames` (1 frame entre capturas,
# CPU congelada en el ready probe). Con captura wall-clock los frames quedaban a ~8
# frames de distancia, fuera del radio de alineacion (±4 px) del analizador de
# visibilidad de tiles, que reportaba falsos `visible_tile_redraw_detected`. Con
# frames consecutivos la alineacion es exacta y el chequeo es significativo.
#
# Uso: analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
DEMO="demos/amiga/105_tile_scroll_xyunlimited_dualpf"
SEQ="$ROOT/out/run/105_tile_scroll_xyunlimited_dualpf/A500_debug/sequence"

WARP=0
while [ "$#" -gt 0 ]; do
	case "$1" in
		--warp) WARP=1; shift ;;
		--pixel-assert|--require-pixel-assert-ok|--vision-review|--require-vision-review-ok) shift ;;
		--vision-provider|--vision-send-mode) shift 2 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

"$ROOT/tools/run/run-demo.sh" "$DEMO" --settle-ms 500 \
	--sequence-step-frames 40 "${extra[@]}"
node "$ROOT/tools/analyze/analyze_105_tile_visibility.mjs" "$SEQ"
"$ROOT/tools/analyze/analyze-frame-sequence.sh" "$SEQ" --expect-animated
echo "OK 105_tile_scroll_xyunlimited_dualpf sequence"
