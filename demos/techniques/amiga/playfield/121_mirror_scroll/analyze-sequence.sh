#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 121 (espejo vertical): build + run + verificación.
#   bash demos/techniques/amiga/playfield/121_mirror_scroll/analyze-sequence.sh [--warp] [--config A500_debug]
#
# 1) La secuencia debe estar ANIMADA (el mundo se desplaza bajo la ventana).
# 2) La telemetría `detail` lleva el marcador 0x13 y la cámara (camX/camY).
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
cd "$ROOT"

CONFIG=A500_debug
WARP=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --warp) WARP=(--warp) ;;
    --config) CONFIG="$2"; shift ;;
    --release) CONFIG=A500_release ;;
    --pixel-assert|--require-pixel-assert-ok|--vision-review|--require-vision-review-ok) : ;;
    --vision-provider|--vision-send-mode) shift ;;
    *) echo "arg desconocido: $1" >&2; exit 2 ;;
  esac
  shift
done

echo "[121] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/121_mirror_scroll --"$MODE" --clean >/dev/null

echo "[121] run + secuencia..."
bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/121_mirror_scroll --config "$CONFIG" \
  --sequence-frames 6 --sequence-interval-ms 250 --side-channel-timeout-ms 60000 "${WARP[@]}" >/dev/null

echo "[121] verificación de movimiento (secuencia animada)..."
node ./dist/tools/analyze/analyze_frame_sequence.js \
  "./out/run/121_mirror_scroll/$CONFIG/sequence" --min-frames 4 --expect-animated

echo "[121] telemetría de cámara (marcador 0x13)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/121_mirror_scroll/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = (d >>> 12) & 0xff;
const y = d & 0xfff;
console.log(`[verify-121] marker=0x${marker.toString(16)} camX=${x} camY=${y}`);
const ok = marker === 0x13 && (y > 0 || x > 0);
console.log(ok ? '[verify-121] PASS: espejo vertical corriendo con telemetría de cámara'
               : '[verify-121] FAIL: marcador ausente o cámara sin movimiento');
process.exit(ok ? 0 : 1);
NODE
