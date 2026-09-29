#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 111 (side-scroller horizontal): build + run +
# verificación de movimiento y de scroll de 2 px/frame.
#
#   bash demos/techniques/amiga/playfield/111_xlimited_sidescroller/analyze-sequence.sh [--warp] [--config A500_debug]
#
# La captura es **frame-step** (frames consecutivos, 1 frame entre capturas): el
# contrato `pixel-contract.json` comprueba con `shifted_region_match` que el contenido
# se desplaza 2 px lógicos por frame (2 px/frame = `ScrollProgressive`). En el tilemap
# el cruce de word se hace por puntero coarse (no se reescribe el buffer), así que
# todos los pares —incluido el wrap— son limpios.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/playfield/111_xlimited_sidescroller"
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

echo "[111] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --"$MODE" --clean >/dev/null

echo "[111] captura step + scroll 2 px/frame..."
"$STEP_SHIFT" --demo "$DEMO" --config "$CONFIG" --contract "$PIXEL_CONTRACT" \
  --frames 8 --settle-ms 1200 "${extra[@]}" \
  || { echo "[111] el scroll no es de 2 px/frame." >&2; exit 1; }

echo "[111] verificación de movimiento (secuencia animada)..."
"$SEQ_ANALYZER" "$ROOT/out/run/111_xlimited_sidescroller/$CONFIG/sequence" \
  --min-frames 4 --expect-animated

echo "[111] telemetría de cámara (marcador 0x11, cameraX en bits 16-23)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/111_xlimited_sidescroller/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = ((d >>> 16) & 0xff) | ((d & 0xff) << 8);
console.log(`[verify-111] marker=0x${marker.toString(16)} camX=${x}`);
const ok = marker === 0x11 && x > 0;
console.log(ok ? '[verify-111] PASS: side-scroller corriendo y avanzando en X'
               : '[verify-111] FAIL: marcador/X ausente');
process.exit(ok ? 0 : 1);
NODE

echo "OK 111_xlimited_sidescroller sequence"
