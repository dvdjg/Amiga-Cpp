#!/usr/bin/env bash
# Build + run the on-target audio leases canary in WinUAE, then assert its run-status.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/002_audio_leases"
REPORT="$ROOT/out/run/002_audio_leases/A500_debug/run-report.json"

if [[ "${1:-}" != "--skip-build" ]]; then
	bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
fi
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 --screenshot "$ROOT/out/tmp/002_audio_leases.png"

node - "$REPORT" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const status = report.finalSideChannel;
const detail = status?.detail ?? 0;
const checks = detail & 0x3f;
const dmacon = (detail >>> 16) & 0xffff;
if (report.status !== 'ok' || status?.state !== 3 || checks !== 0x3f) {
  console.error('[L1-002] FAIL: se esperaba READY con los 6 checks (4 canales, rechazo por prioridad, reocupar tras stop, AUD0EN y DMA de audio apagado).');
  console.error(JSON.stringify({status: report.status, finalSideChannel: status, checks: checks.toString(2)}, null, 2));
  process.exit(1);
}
if ((dmacon & 0x000f) !== 0x0001) {
  console.error(`[L1-002] FAIL: DMACONR con SFX activo deberia tener AUD0EN (0x0001), es 0x${(dmacon & 0xf).toString(16)}.`);
  process.exit(1);
}
console.log('[L1-002] OK: 4 canales con leases, rechazo por prioridad, liberar/reocupar y DMA de audio (AUD0EN) verificado en target.');
NODE
