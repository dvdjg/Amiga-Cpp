#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 120 (virtual playfield): build + run + verificación.
#   bash demos/amiga/120_virtual_playfield/analyze-sequence.sh [--warp] [--config A500_debug]
#
# 1) La secuencia debe estar ANIMADA (analyze_frame_sequence --expect-animated):
#    el mundo se desplaza bajo una ventana fija.
# 2) La telemetría `detail` lleva el marcador 0x12 y la cámara (camX/camY).
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

echo "[120] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash ./tools/build/build-demo.sh demos/amiga/120_virtual_playfield --"$MODE" --clean >/dev/null

echo "[120] run + secuencia..."
bash ./tools/run/run-demo.sh demos/amiga/120_virtual_playfield --config "$CONFIG" \
  --sequence-frames 6 --sequence-interval-ms 250 --side-channel-timeout-ms 60000 "${WARP[@]}" >/dev/null

echo "[120] verificación de movimiento (secuencia animada)..."
node ./dist/tools/analyze/analyze_frame_sequence.js \
  "./out/run/120_virtual_playfield/$CONFIG/sequence" --min-frames 4 --expect-animated

echo "[120] telemetría de cámara (marcador 0x12)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/120_virtual_playfield/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = (d >>> 12) & 0xff;
const y = d & 0xfff;
console.log(`[verify-120] marker=0x${marker.toString(16)} camX=${x} camY=${y}`);
const ok = marker === 0x12 && (x > 0 || y > 0);
console.log(ok ? '[verify-120] PASS: virtual playfield corriendo con telemetría de cámara'
               : '[verify-120] FAIL: marcador ausente o cámara sin movimiento');
process.exit(ok ? 0 : 1);
NODE
