#!/usr/bin/env bash
# Build + run the SpriteDirector state probe and assert READY/count/canaries.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/006_director_state"
REPORT="$ROOT/out/run/006_director_state/A500_debug/run-report.json"
WANT=0xbeef0801

if [[ "${1:-}" != "--skip-build" ]]; then
	bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
fi
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 800 \
	--screenshot "$ROOT/out/tmp/006_director_state.png" >/dev/null

node - "$REPORT" "$WANT" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const want = Number(process.argv[3]);
const status = report.finalSideChannel;
const detail = status?.detail ?? -1;
if (report.status !== 'ok' || status?.state !== 3 || detail !== want) {
  console.error('[L1-006] FAIL: se esperaba READY/detail=0xbeef0801 (8 entidades y canarios intactos).');
  console.error(JSON.stringify({status: report.status, finalSideChannel: status}, null, 2));
  process.exit(1);
}
console.log('[L1-006] OK: 8 entidades tras init, canarios intactos (detail=0xbeef0801).');
NODE
