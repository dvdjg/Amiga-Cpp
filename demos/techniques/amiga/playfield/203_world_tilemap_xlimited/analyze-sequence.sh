#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/playfield/203_world_tilemap_xlimited"
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
MODE=debug; [ "$CONFIG" = A500_release ] && MODE=release
bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --"$MODE" --clean >/dev/null
ARGS=(--demo "$DEMO" --config "$CONFIG" --contract "$(dirname "${BASH_SOURCE[0]}")/pixel-contract.json" --frames 8 --settle-ms 1200)
[ "$WARP" -eq 1 ] && ARGS+=(--warp)
bash "$ROOT/tools/analyze/step-shift-check.sh" "${ARGS[@]}"
bash "$ROOT/tools/analyze/analyze-frame-sequence.sh" \
	"$ROOT/out/run/203_world_tilemap_xlimited/$CONFIG/sequence" --min-frames 4 --expect-animated
node - "$CONFIG" <<'NODE'
const cfg = process.argv[2];
const r = require('./out/run/203_world_tilemap_xlimited/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value && r.sideChannel.value.detail) || 0;
const marker = (d >>> 24) & 0xff;
const x = ((d >>> 16) & 0xff) | (d & 0xff) << 8;
console.log(`[verify-203] marker=0x${marker.toString(16)} cameraX=${x}`);
const ok = marker === 0x20 && x > 0;
console.log(ok ? '[verify-203] PASS: capa World conduce el scroll XLimited' : '[verify-203] FAIL: falta marcador o avance');
process.exit(ok ? 0 : 1);
NODE
