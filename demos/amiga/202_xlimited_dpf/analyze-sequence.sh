#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# analyze-sequence.sh — Demo 202 (DPF 3+3): build + run + verificación.
#   bash demos/amiga/202_xlimited_dpf/analyze-sequence.sh [--warp] [--config A500_debug]
# 1) Verifica que ambas capas están en movimiento continuo (verify-parallax.mjs).
# 2) Regresión de la Y INDEPENDIENTE: con warp y un settle largo el BG (split)
#    recorre el mundo verticalmente (bgY alto) mientras el FG lineal mantiene su
#    propia Y (fgY ≤ 128). Si `kShareY=true` (corkscrew dual), fgY==bgY y el gate
#    falla. Se lee el `detail` del run-status: phase<<24 | fgY<<12 | bgY.
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

echo "[202] build (${CONFIG})..."
bash ./tools/build/build-demo.sh demos/amiga/202_xlimited_dpf "--$([ "$CONFIG" = A500_release ] && echo release || echo debug)" --clean >/dev/null

echo "[202] run + secuencia..."
bash ./tools/run/run-demo.sh demos/amiga/202_xlimited_dpf --config "$CONFIG" --sequence-frames 3 --sequence-interval-ms 150 "${WARP[@]}" >/dev/null

echo "[202] verificación (ambas capas en movimiento continuo)..."
node ./demos/amiga/202_xlimited_dpf/verify-parallax.mjs --config "$CONFIG"

echo "[202] regresión Y independiente (warp, settle largo)..."
bash ./tools/run/run-demo.sh demos/amiga/202_xlimited_dpf --config "$CONFIG" --warp --settle-ms 55000 --sequence-frames 0 >/dev/null
node - "$CONFIG" <<'NODE'
const fs = require('fs');
const cfg = process.argv[2];
const r = require('./out/run/202_xlimited_dpf/' + cfg + '/run-report.json');
const d = (r.finalSideChannel && r.finalSideChannel.detail) || (r.sideChannel && r.sideChannel.value.detail);
const phase = (d >>> 24) & 0xff, maxYDelta = (d >>> 12) & 0x3ff, bgY = d & 0x3ff;
console.log('[verify-202] phase=' + phase + ' maxΔY=' + maxYDelta + ' bgY=' + bgY);
// maxΔY = máx|fgY-bgY| acumulado en toda la ejecución. En el modo independencia
// el FG lineal (Y 0..128) no sigue al BG split (recorre 0..432) → maxΔY alto.
// En corkscrew dual compartido fgY==bgY → maxΔY ≈ 0 (debe fallar).
const ok = maxYDelta >= 60;
console.log(ok
  ? '[verify-202] PASS: la Y del FG es independiente de la del BG'
  : '[verify-202] FAIL: las Y no son independientes (maxΔY≈0). ¿kShareY=true?');
process.exit(ok ? 0 : 1);
NODE
