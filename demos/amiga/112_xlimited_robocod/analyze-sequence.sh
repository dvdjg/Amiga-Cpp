#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 112 (XYLimited 5 planos, fondo RoboCod): build +
# run + verificación de movimiento.
# ---------------------------------------------------------------------------
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

CONFIG=A500_debug
WARP=()
while [ "$#" -gt 0 ]; do
  case "$1" in
    --warp) WARP=(--warp) ;;
    --config) CONFIG="$2"; shift ;;
    --release) CONFIG=A500_release ;;
    *) echo "arg desconocido: $1" >&2; exit 2 ;;
  esac
  shift
done

echo "[112] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash ./tools/build/build-demo.sh demos/amiga/112_xlimited_robocod --"$MODE" --clean >/dev/null

echo "[112] run + secuencia..."
bash ./tools/run/run-demo.sh demos/amiga/112_xlimited_robocod --config "$CONFIG" \
  --sequence-frames 6 --sequence-interval-ms 250 --side-channel-timeout-ms 60000 "${WARP[@]}" >/dev/null

echo "[112] verificación de movimiento (secuencia animada)..."
node ./dist/tools/analyze/analyze_frame_sequence.js \
  "./out/run/112_xlimited_robocod/$CONFIG/sequence" --min-frames 4 --expect-animated

echo "[112] telemetría de cámara (marcador 0x11)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/112_xlimited_robocod/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = (d >>> 8) & 0xffff;
console.log(`[verify-112] marker=0x${marker.toString(16)} camX=${x}`);
console.log(marker === 0x11 ? '[verify-112] PASS: RoboCod (plano de fondo con parallax) corriendo'
                            : '[verify-112] FAIL: marcador ausente');
process.exit(marker === 0x11 ? 0 : 1);
NODE
