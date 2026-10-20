#!/usr/bin/env bash
# Build + run the actors-cost probe and print the per-vertex costs from the run-status detail.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/004_actors_cost"
REPORT="$ROOT/out/run/004_actors_cost/A500_debug/run-report.json"

if [[ "${1:-}" != "--skip-build" ]]; then
	bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
fi
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 800 \
	--screenshot "$ROOT/out/tmp/004_actors_cost.png" >/dev/null

node - "$REPORT" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const status = report.finalSideChannel;
const d = status?.detail ?? 0;
const transform = ((d >> 0) & 0x3ff) * 4;
const frustum = ((d >> 10) & 0x3ff) * 4;
const project = ((d >> 20) & 0x3ff) * 4;
if (report.status !== 'ok' || status?.state !== 3) {
  console.error('[L1-004] FAIL: la sonda no alcanzó READY.');
  console.error(JSON.stringify({status: report.status, finalSideChannel: status}, null, 2));
  process.exit(1);
}
if (transform === 0 || frustum === 0 || project === 0) {
  console.error(`[L1-004] FAIL: medida nula (transform=${transform} frustum=${frustum} proyeccion=${project}).`);
  process.exit(1);
}
console.log(`[L1-004] OK: coste/vértice transform=${transform} frustum=${frustum} proyeccion=${project} ciclos (detail=0x${d.toString(16)}).`);
NODE
