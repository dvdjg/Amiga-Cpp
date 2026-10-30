#!/usr/bin/env bash
# Build + run the two-BOB base/footprint probe and assert READY detail=31.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/007_bob_plane_base"
REPORT="$ROOT/out/run/007_bob_plane_base/A500_debug/run-report.json"

if [[ "${1:-}" != "--skip-build" ]]; then
	bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
fi
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 \
	--screenshot "$ROOT/out/tmp/007_bob_plane_base.png"

node - "$REPORT" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const status = report.finalSideChannel;
const detail = status?.detail ?? -1;
if (report.status !== 'ok' || status?.state !== 3 || detail !== 31) {
  console.error('[L1-007] FAIL: se esperaba READY/detail=31 (ejecucion, pixel, canarios y plan).');
  console.error(JSON.stringify({status: report.status, finalSideChannel: status}, null, 2));
  process.exit(1);
}
console.log('[L1-007] OK: dos BOBs, huella 2 palabras, canarios y plan validos.');
NODE
