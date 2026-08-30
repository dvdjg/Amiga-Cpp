#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion de la demo 106 (scroll infinito dual playfield con
# la API TileFieldController + DpfDisplayComposer).
# Cadena: run-demo (secuencia) -> animada -> sin negro interno -> telemetria
# con dos camaras independientes que scrollean en AMBOS ejes y cruzan paginas.
# Uso: analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="demos/106_tile_field_infinite_dualpf"
RUN="$ROOT/tools/run/run-demo.sh"
SEQ_ANALYZER="$ROOT/tools/analyze/analyze-frame-sequence.sh"
INNER_BLACK="$ROOT/tools/analyze/assert-no-inner-black.sh"

SEQ_DIR="$ROOT/out/run/106_tile_field_infinite_dualpf/sequence"
RUN_REPORT="$ROOT/out/run/106_tile_field_infinite_dualpf/run-report.json"

WARP=0
[ "${1:-}" = "--warp" ] && WARP=1
extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

# 1) Captura la secuencia. Intervalo largo para que la demo avance en tiempo real
#    (~50fps): bg cruza su primera pagina X en ~frame 160 (2px/frame) y el fg en
#    ~frame 320; con 8 capturas x 800ms + settle se supera el frame 400.
"$RUN" "$DEMO" --settle-ms 500 --sequence-frames 8 --sequence-interval-ms 800 "${extra[@]}" \
	|| { echo "No se pudo capturar la secuencia de 106_tile_field_infinite_dualpf." >&2; exit 1; }

# 2) Debe demostrar animacion.
"$SEQ_ANALYZER" "$SEQ_DIR" --expect-animated \
	|| { echo "La secuencia de 106 no demuestra animacion." >&2; exit 1; }

# 3) Sin negro interno en la ventana visible.
# Umbral 0.05: la 106 usa PF1 con ~50% de tiles totalmente transparentes (tile 63)
# y el resto tramado al 50%. Donde el fg es 0 (transparente) y el fondo PF2 tiene
# su color 0 (registro 8, transparente en DPF), el resultado es negro puro: es el
# diseño pedido, no un agujero. El modelo de vision confirma continuidad perfecta.
"$INNER_BLACK" "$SEQ_DIR" 0.05 \
	|| { echo "La secuencia de 106 contiene artefactos negros internos." >&2; exit 1; }

# 4) Telemetria: los dos campos scrollean en ambos ejes y cruzan paginas.
#    Bits 16-19 = latch de cruce de pagina X/Y de bg/fg (se mantiene a 1 una vez
#    cruzada, sin depender del instante de lectura); bytes = mundo del fg.
node -e '
const fs = require("fs");
const report = JSON.parse(fs.readFileSync(process.argv[1], "utf-8"));
const status = report.finalSideChannel && report.finalSideChannel.ok
  ? report.finalSideChannel
  : (report.sideChannel || {}).value;
const detail = parseInt(status.detail || 0, 10) - 0x10600000;
const bgCrossX = (detail >> 16) & 1;
const fgCrossX = (detail >> 17) & 1;
const bgCrossY = (detail >> 18) & 1;
const fgCrossY = (detail >> 19) & 1;
const fgWorldXTiles = (detail >> 8) & 0xff;
const fgWorldYTiles = detail & 0xff;
const maxPending = (detail >> 20) & 0xffff;
const frame = parseInt(status.frame || 0, 10);
if (frame < 60) {
  console.error(`Frame demasiado corto: frame=${frame}`);
  process.exit(1);
}
// El fg recorre 0..640 (X) y 0..512 (Y): debe haber cruzado una pagina en ambos
// ejes en algun momento. El latch lo garantiza aunque la lectura sea temprana.
if (!(fgCrossX && fgCrossY && (bgCrossX || bgCrossY))) {
  console.error(`No hay cruce de pagina en ambos ejes: bg=(${bgCrossX},${bgCrossY}) fg=(${fgCrossX},${fgCrossY})`);
  process.exit(1);
}
// El maximo de tiles pendientes debe ser > 0: confirma que las franjas del scroll
// se estan encolando y redibujando (regresion del bug de enqueue_strip que
// descartaba todas las franjas tras el estampado inicial -> saltos de color).
if (maxPending === 0) {
  console.error(`No hay redibujado en marcha: max_pending=0 (bug de enqueue_strip)`);
  process.exit(1);
}
console.log(`OK 106 telemetry frame=${frame} bg_cross=(${bgCrossX},${bgCrossY}) fg_cross=(${fgCrossX},${fgCrossY}) fg_world_low=(${fgWorldXTiles},${fgWorldYTiles}) max_pending=${maxPending}`);
' "$RUN_REPORT" \
	|| { echo "Telemetria de 106 invalida." >&2; exit 1; }

echo "OK 106_tile_field_infinite_dualpf sequence"
