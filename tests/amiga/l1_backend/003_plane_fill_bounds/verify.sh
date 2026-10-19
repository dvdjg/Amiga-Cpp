#!/usr/bin/env bash
# Build + run the plane-fill bounds integration test in WinUAE, then assert its run-status.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/003_plane_fill_bounds"
REPORT="$ROOT/out/run/003_plane_fill_bounds/A500_debug/run-report.json"

if [[ "${1:-}" != "--skip-build" ]]; then
	bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
fi
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 \
	--screenshot "$ROOT/out/tmp/003_plane_fill_bounds.png"

node - "$REPORT" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const status = report.finalSideChannel;
const detail = status?.detail ?? -1;
// 8 bits: residuo, mundo, bordes, interiores, nada perdido (+-1 px), fila 0 limpia,
// adyacencia entre planos (sin magenta/hueco) y junta contigua.
const want = 0xff;
if (report.status !== 'ok' || status?.state !== 3 || detail !== want) {
  console.error('[L1-003] FAIL: se esperaba READY/detail=0xff (sin residuo, mundo y bordes limpios, interiores rellenos, nada perdido respecto a la CPU, fila 0 limpia y adyacencia entre planos correcta).');
  console.error(JSON.stringify({status: report.status, finalSideChannel: status}, null, 2));
  process.exit(1);
}
console.log('[L1-003] OK: relleno por plano con caras en x=320, x=-1 e y=232, sin residuo, con fila 0 limpia y junta entre planos de un solo color.');
NODE
