#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion de la demo 102 (dual playfield 2+3 con parallax).
# Cadena: run-demo (secuencia) -> animada -> sin negro interno -> telemetria
# con dos camaras independientes y prefetch de ambas capas.
# Uso: analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="demos/102_tile_scroll_dualpf"
RUN="$ROOT/tools/run/run-demo.sh"
SEQ_ANALYZER="$ROOT/tools/analyze/analyze-frame-sequence.sh"
INNER_BLACK="$ROOT/tools/analyze/assert-no-inner-black.sh"

SEQ_DIR="$ROOT/out/run/102_tile_scroll_dualpf/sequence"
RUN_REPORT="$ROOT/out/run/102_tile_scroll_dualpf/run-report.json"

WARP=0
[ "${1:-}" = "--warp" ] && WARP=1
extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

# 1) Captura la secuencia.
"$RUN" "$DEMO" --settle-ms 500 --sequence-frames 8 --sequence-interval-ms 150 "${extra[@]}" \
	|| { echo "No se pudo capturar la secuencia de 102_tile_scroll_dualpf." >&2; exit 1; }

# 2) Debe demostrar animacion.
"$SEQ_ANALYZER" "$SEQ_DIR" --expect-animated \
	|| { echo "La secuencia de 102 no demuestra animacion." >&2; exit 1; }

# 3) Sin negro interno en la ventana visible.
"$INNER_BLACK" "$SEQ_DIR" 0.001 \
	|| { echo "La secuencia de 102 contiene artefactos negros internos." >&2; exit 1; }

# 4) Telemetria: dos camaras moviendose en direcciones opuestas + prefetch X/Y.
node -e '
const fs = require("fs");
const report = JSON.parse(fs.readFileSync(process.argv[1], "utf-8"));
const status = report.finalSideChannel && report.finalSideChannel.ok
  ? report.finalSideChannel
  : (report.sideChannel || {}).value;
const detail = parseInt(status.detail || 0, 10);
const bgX = (detail >> 16) & 0xff;
const fgX = (detail >> 8) & 0xff;
const flags = detail & 0x0f;
const frame = parseInt(status.frame || 0, 10);
if (frame < 60 || (flags & 0x3) !== 0x3 || Math.abs(bgX - fgX) < 8) {
  console.error(`DualPF invalido: frame=${frame} bg_x=${bgX} fg_x=${fgX} flags=${flags}`);
  process.exit(1);
}
console.log(`OK dualpf telemetry frame=${frame} bg_x=${bgX} fg_x=${fgX} flags=${flags}`);
' "$RUN_REPORT" \
	|| { echo "Telemetria de 102 invalida." >&2; exit 1; }

echo "OK 102_tile_scroll_dualpf sequence"
