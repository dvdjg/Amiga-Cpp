#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 110 (shooter vertical, Y-limited): build + run +
# verificación de movimiento.
#   bash demos/amiga/110_ylimited_shooter/analyze-sequence.sh [--warp] [--config A500_debug]
#
# 1) La secuencia debe estar ANIMADA (analyze_frame_sequence --expect-animated):
#    el BG scrollea y los objetos (nave/balas) se mueven.
# 2) La telemetría `detail` lleva el marcador 0x11 y la cámara (mapposx/mapposy).
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

echo "[110] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash ./tools/build/build-demo.sh demos/amiga/110_ylimited_shooter --"$MODE" --clean >/dev/null

echo "[110] run + secuencia..."
bash ./tools/run/run-demo.sh demos/amiga/110_ylimited_shooter --config "$CONFIG" \
  --sequence-frames 6 --sequence-interval-ms 250 --side-channel-timeout-ms 60000 "${WARP[@]}" >/dev/null

echo "[110] verificación de movimiento (secuencia animada)..."
node ./dist/tools/analyze/analyze_frame_sequence.js \
  "./out/run/110_ylimited_shooter/$CONFIG/sequence" --min-frames 4 --expect-animated

echo "[110] telemetría de cámara (marcador 0x11)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/110_ylimited_shooter/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = (d >>> 10) & 0x3ff;
const y = d & 0x3ff;
console.log(`[verify-110] marker=0x${marker.toString(16)} camX=${x} camY=${y}`);
const ok = marker === 0x11;
console.log(ok ? '[verify-110] PASS: demo 110 corriendo con telemetría de cámara'
               : '[verify-110] FAIL: marcador ausente (¿demo no lista?)');
process.exit(ok ? 0 : 1);
NODE
