#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 111 (side-scroller horizontal): build + run +
# verificación de movimiento.
#   bash demos/amiga/111_xlimited_sidescroller/analyze-sequence.sh [--warp] [--config A500_debug]
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

echo "[111] build (${CONFIG})..."
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash ./tools/build/build-demo.sh demos/amiga/111_xlimited_sidescroller --"$MODE" --clean >/dev/null

echo "[111] run + secuencia..."
bash ./tools/run/run-demo.sh demos/amiga/111_xlimited_sidescroller --config "$CONFIG" \
  --sequence-frames 6 --sequence-interval-ms 250 --side-channel-timeout-ms 60000 "${WARP[@]}" >/dev/null

echo "[111] verificación de movimiento (secuencia animada)..."
node ./dist/tools/analyze/analyze_frame_sequence.js \
  "./out/run/111_xlimited_sidescroller/$CONFIG/sequence" --min-frames 4 --expect-animated

echo "[111] telemetría de cámara (marcador 0x11)..."
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/111_xlimited_sidescroller/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = (d >>> 8) & 0xffff;
console.log(`[verify-111] marker=0x${marker.toString(16)} camX=${x}`);
const ok = marker === 0x11 && x > 0;
console.log(ok ? '[verify-111] PASS: side-scroller corriendo y avanzando en X'
               : '[verify-111] FAIL: marcador/X ausente');
process.exit(ok ? 0 : 1);
NODE
