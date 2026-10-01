#!/usr/bin/env bash
# Build + run the App display teardown integration test in WinUAE, then assert its run-status.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/001_app_display_teardown"
REPORT="$ROOT/out/run/001_app_display_teardown/A500_debug/run-report.json"

if [[ "${1:-}" != "--skip-build" ]]; then
	bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
fi
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 --screenshot "$ROOT/out/tmp/001_app_display_teardown.png"

node - "$REPORT" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const status = report.finalSideChannel;
if (report.status !== 'ok' || status?.state !== 3 || status?.detail !== 7) {
  console.error('[L1-001] FAIL: expected READY/detail=7 (display stopped, Chip pool reclaimed, App started).');
  console.error(JSON.stringify({status: report.status, finalSideChannel: status}, null, 2));
  process.exit(1);
}
console.log('[L1-001] OK: App started, DMA is idle, scene Chip allocations are reclaimed.');
NODE
