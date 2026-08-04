#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion temporal de la demo 101_ehb_tile_scroll_driver.
# Sustituye a analyze-sequence.ps1. Cadena:
#   run-demo (secuencia 12 frames) -> analyze-frame-sequence (-ExpectAnimated)
#   -> assert-no-inner-black -> telemetria run-report (frame>=200, camera<=128,
#   prefetch X/Y) -> assert-pixel-contract (opcional) -> FrameScope
#   (amiga-scroll) -> analyze-fine-scroll -> Vision Review (opcional).
# Uso: analyze-sequence.sh [--warp] [--pixel-assert] [--require-pixel-assert-ok]
#       [--vision-review] [--require-vision-review-ok]
#       [--vision-provider <ruta>] [--vision-send-mode <modo>]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="demos/101_ehb_tile_scroll_driver"
RUN="$ROOT/tools/run/run-demo.sh"
SEQ_ANALYZER="$ROOT/tools/analyze/analyze-frame-sequence.sh"
INNER_BLACK="$ROOT/tools/analyze/assert-no-inner-black.sh"
PIXEL_ASSERT="$ROOT/tools/analyze/assert-pixel-contract.sh"
FRAME_SCOPE="$ROOT/tools/framescope/frame-scope.sh"
VISION_REVIEW="$ROOT/tools/vision-review/vision-review.sh"
FINE_SCROLL="$(dirname "${BASH_SOURCE[0]}")/analyze-fine-scroll.sh"
PIXEL_CONTRACT="$(dirname "${BASH_SOURCE[0]}")/pixel-contract.json"

SEQ_DIR="$ROOT/out/run/101_ehb_tile_scroll_driver/sequence"
RUN_REPORT="$ROOT/out/run/101_ehb_tile_scroll_driver/run-report.json"
FRAME_SCOPE_OUT="$ROOT/out/framescope/101_ehb_tile_scroll_driver"
VISION_OUT="$ROOT/out/vision-review/101_ehb_tile_scroll_driver"
PIXEL_OUT="$ROOT/out/analysis/101_ehb_tile_scroll_driver/pixel-assert"

WARP=0; PA=0; REQUIRE_PA=0; VR=0; REQUIRE_VR=0
VISION_PROVIDER=""
VISION_SEND_MODE="multi-image"
while [ "$#" -gt 0 ]; do
	case "$1" in
		--warp) WARP=1; shift ;;
		--pixel-assert) PA=1; shift ;;
		--require-pixel-assert-ok) PA=1; REQUIRE_PA=1; shift ;;
		--vision-review) VR=1; shift ;;
		--require-vision-review-ok) VR=1; REQUIRE_VR=1; shift ;;
		--vision-provider) VISION_PROVIDER="$2"; shift 2 ;;
		--vision-send-mode) VISION_SEND_MODE="$2"; shift 2 ;;
		*) echo "Argumento desconocido: $1" >&2; exit 2 ;;
	esac
done

# 1) Captura la secuencia animada de 12 frames.
extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)
"$RUN" "$DEMO" --settle-ms 3500 --sequence-frames 12 --sequence-interval-ms 120 "${extra[@]}" \
	|| { echo "No se pudo capturar la secuencia animada de 101_ehb_tile_scroll_driver." >&2; exit 1; }

# 2) La secuencia debe demostrar animacion.
"$SEQ_ANALYZER" "$SEQ_DIR" --expect-animated \
	|| { echo "La secuencia de 101_ehb_tile_scroll_driver no demuestra animacion." >&2; exit 1; }

# 3) Sin negro interno (reinicio de Copper / puntero de bitplane a media pantalla).
"$INNER_BLACK" "$SEQ_DIR" 0.001 \
	|| { echo "La secuencia contiene artefactos negros internos." >&2; exit 1; }

# 4) Telemetria: la fase circular con prefetch X/Y valido.
python3 - "$RUN_REPORT" <<'PY'
import json, sys
report_path = sys.argv[1]
report = json.loads(open(report_path, encoding="utf-8").read())
status = report.get("finalSideChannel") if report.get("finalSideChannel", {}).get("ok") else (report.get("sideChannel") or {}).get("value")
detail = int(status.get("detail", 0))
camera_x = (detail >> 16) & 0xff
camera_y = (detail >> 8) & 0xff
prefetch = detail & 0x0f
frame = int(status.get("frame", 0))
if frame < 200 or camera_x > 128 or camera_y > 128 or (prefetch & 0x3) != 0x3:
    raise SystemExit(f"La secuencia no alcanzo la fase circular con prefetch X/Y valido: frame={frame} detail=0x{detail:08x}")
print(f"OK telemetry frame={frame} detail=0x{detail:08x}")
PY
[ $? -eq 0 ] || { echo "Telemetria de 101 invalida." >&2; exit 1; }

# 5) Pixel Assertions (opcional pero exigible).
if [ "$PA" -eq 1 ]; then
	"$PIXEL_ASSERT" --sequence-dir "$SEQ_DIR" --contract "$PIXEL_CONTRACT" --run-report "$RUN_REPORT" --out-dir "$PIXEL_OUT" \
		|| { echo "Pixel Assertions detecto una desviacion de render en la secuencia." >&2; exit 1; }
fi

# 6) FrameScope con perfil amiga-scroll.
"$FRAME_SCOPE" --source "$SEQ_DIR" --out-dir "$FRAME_SCOPE_OUT" --profile amiga-scroll \
	--grid-width 64 --grid-height 48 --search-radius 12 --max-profile-mismatches 1 --expect-animated \
	|| { echo "FrameScope no pudo validar el diagnostico amiga-scroll." >&2; exit 1; }

# 7) Transicion de fine scroll 14,15,0,1.
if ! "$FINE_SCROLL" "${extra[@]}"; then
	echo "La transicion fine scroll no es continua." >&2
	exit 1
fi

# 8) Vision Review (opcional).
if [ "$VR" -eq 1 ]; then
	if [ -z "$VISION_PROVIDER" ]; then
		VISION_PROVIDER="$ROOT/tools/vision-review/providers/lmstudio.legion.json"
	fi
	vr_args=(--source "$SEQ_DIR" --run-report "$RUN_REPORT"
		--frame-scope-report "$FRAME_SCOPE_OUT/framescope-report.json"
		--profile amiga-scroll-transition --provider "$VISION_PROVIDER"
		--send-mode "$VISION_SEND_MODE" --out-dir "$VISION_OUT")
	[ "$REQUIRE_VR" -eq 1 ] && vr_args+=(--require-ok)
	if ! "$VISION_REVIEW" "${vr_args[@]}"; then
		echo "Vision Review no pudo validar la transicion amiga-scroll." >&2
		exit 1
	fi
fi

echo "OK 101_ehb_tile_scroll_driver sequence"
