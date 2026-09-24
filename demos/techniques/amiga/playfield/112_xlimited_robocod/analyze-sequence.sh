#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 112 (RoboCod: plano de fondo con parallax): build +
# run + verificación de movimiento y de scroll del fondo.
#
#   bash demos/techniques/amiga/playfield/112_xlimited_robocod/analyze-sequence.sh [--warp] [--config A500_debug]
#
# La captura es **frame-step** (frames consecutivos): el contrato comprueba con
# `shifted_region_match` que el plano de fondo se desplaza 1 px lógico por frame
# (el parallax lo mueve a la mitad de la cámara, que avanza a 2 px/frame). En el
# tilemap el cruce de word es por puntero coarse (no reescribe el buffer) → sin
# transitorio.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/playfield/112_xlimited_robocod"
STEP_SHIFT="$ROOT/tools/analyze/step-shift-check.sh"
SEQ_ANALYZER="$ROOT/tools/analyze/analyze-frame-sequence.sh"
PIXEL_CONTRACT="$(dirname "${BASH_SOURCE[0]}")/pixel-contract.json"

CONFIG=A500_debug
WARP=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --warp) WARP=1; shift ;;
    --config) CONFIG="$2"; shift 2 ;;
    --release) CONFIG=A500_release; shift ;;
    --pixel-assert|--require-pixel-assert-ok|--vision-review|--require-vision-review-ok) shift ;;
    --vision-provider|--vision-send-mode) shift 2 ;;
    *) echo "arg desconocido: $1" >&2; exit 2 ;;
  esac
done

extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

echo "[112] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --"$MODE" --clean >/dev/null

echo "[112] captura step + scroll del fondo 1 px/frame..."
"$STEP_SHIFT" --demo "$DEMO" --config "$CONFIG" --contract "$PIXEL_CONTRACT" \
  --frames 8 --settle-ms 1200 "${extra[@]}" \
  || { echo "[112] el fondo no scrollea de forma continua." >&2; exit 1; }

echo "[112] verificación de movimiento (secuencia animada)..."
"$SEQ_ANALYZER" "$ROOT/out/run/112_xlimited_robocod/$CONFIG/sequence" \
  --min-frames 4 --expect-animated

echo "[112] telemetría de cámara (marcador 0x11, cameraX en bits 16-23)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/112_xlimited_robocod/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = ((d >>> 16) & 0xff) | ((d & 0xff) << 8);
console.log(`[verify-112] marker=0x${marker.toString(16)} camX=${x}`);
console.log(marker === 0x11 ? '[verify-112] PASS: RoboCod (plano de fondo con parallax) corriendo'
                            : '[verify-112] FAIL: marcador ausente');
process.exit(marker === 0x11 ? 0 : 1);
NODE

echo "OK 112_xlimited_robocod sequence"
