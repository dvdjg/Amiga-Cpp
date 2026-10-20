#!/usr/bin/env bash
# Build + run the camera-corner clipping test in WinUAE, then assert its run-status.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
DEMO="tests/amiga/l1_backend/005_render3d_corners"
REPORT="$ROOT/out/run/005_render3d_corners/A500_debug/run-report.json"
EDGE_STRESS=0
EDGE_STRESS_CPU=0
if [[ "${1:-}" == "--edge-stress" || "${1:-}" == "--edge-stress-cpu" ]]; then
	EDGE_STRESS=1
	if [[ "${1:-}" == "--edge-stress-cpu" ]]; then EDGE_STRESS_CPU=1; fi
	shift
fi

if [[ "${1:-}" != "--skip-build" ]]; then
	if [[ "$EDGE_STRESS_CPU" == "1" ]]; then
		DEMO_FLAGS="-DK_L1_EDGE_STRESS=1 -DK_L1_EDGE_STRESS_CPU_DISPLAY=1" \
			bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug --clean
	elif [[ "$EDGE_STRESS" == "1" ]]; then
		DEMO_FLAGS="-DK_L1_EDGE_STRESS=1" bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug --clean
	else
		bash "$ROOT/tools/build/build-demo.sh" "$DEMO" --debug
	fi
fi
if [[ "$EDGE_STRESS_CPU" == "1" ]]; then
	bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 \
		--screenshot "$ROOT/out/tmp/005_render3d_edge_stress_cpu.png"
	EXPECTED_DETAIL=0x0f
elif [[ "$EDGE_STRESS" == "1" ]]; then
	bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 \
		--screenshot "$ROOT/out/tmp/005_render3d_edge_stress.png"
	EXPECTED_DETAIL=0x0f
else
	bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --wait-ms 1000 \
		--screenshot "$ROOT/out/tmp/005_render3d_corners.png"
	EXPECTED_DETAIL=0x3f
fi

node - "$REPORT" "$EXPECTED_DETAIL" "$EDGE_STRESS" "$EDGE_STRESS_CPU" <<'NODE'
const fs = require('node:fs');
const report = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const status = report.finalSideChannel;
const detail = status?.detail ?? -1;
const want = Number.parseInt(process.argv[3], 16);
const stress = process.argv[4] === '1';
const observed = stress ? (detail & 0x0f) : detail;
if (report.status !== 'ok' || status?.state !== 3 || observed !== want) {
  console.error(`[L1-005${stress ? '/edge-stress' : ''}] FAIL: se esperaba READY/detail=${process.argv[3]} (observed=0x${observed.toString(16)}).`);
  if (stress) console.error(`[L1-005/edge-stress] cpuColors=0x${((detail >>> 4) & 0xf).toString(16)} borderExceptions=${(detail >>> 8) & 0xff} exactPlaneDifferences=${detail >>> 16}`);
  console.error(JSON.stringify({status: report.status, finalSideChannel: status}, null, 2));
  process.exit(1);
}
if (stress) {
	const cpuDisplay = process.argv[5] === '1';
	console.log(`[L1-005/edge-stress${cpuDisplay ? '/cpu-display' : ''}] OK: pose f0473, máscara CPU=0x${((detail >>> 4) & 0xf).toString(16)}, CPU/Blitter dentro de ±2 px; excepciones de borde=${(detail >>> 8) & 0xff}, diferencias exactas por plano=${detail >>> 16}.`);
} else {
  console.log('[L1-005] OK: cuatro esquinas 3D recortadas; cobertura por plano dentro de ±2 px y sin píxeles fuera de región.');
}
NODE
