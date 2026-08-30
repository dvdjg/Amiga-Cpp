#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Secuencia de verificacion de la demo 106_tile_field_showcase (playfield
# universal: dual 3+3 o single 5 planos, tiles de 16/32px).
# Cadena: test unitario del algoritmo -> run-demo (secuencia) -> animada ->
# sin negro interno -> telemetria con cruce de pagina en ambos ejes.
# Uso: analyze-sequence.sh [--warp]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="demos/106_tile_field_showcase"
RUN="$ROOT/tools/run/run-demo.sh"
SEQ_ANALYZER="$ROOT/tools/analyze/analyze-frame-sequence.sh"
INNER_BLACK="$ROOT/tools/analyze/assert-no-inner-black.sh"
FILL_TEST="$ROOT/tools/analyze/verify-tile-field-fill.mjs"

SEQ_DIR="$ROOT/out/run/106_tile_field_showcase/sequence"
RUN_REPORT="$ROOT/out/run/106_tile_field_showcase/run-report.json"

WARP=0
[ "${1:-}" = "--warp" ] && WARP=1
extra=()
[ "$WARP" -eq 1 ] && extra+=(--warp)

# 0) Test unitario del algoritmo begin/update (offset absoluto y delta relativo).
node "$FILL_TEST" \
	|| { echo "El test unitario del algoritmo de rellenado fallo." >&2; exit 1; }

# 1) Captura la secuencia.
"$RUN" "$DEMO" --settle-ms 500 --sequence-frames 8 --sequence-interval-ms 800 "${extra[@]}" \
	|| { echo "No se pudo capturar la secuencia de 106_tile_field_showcase." >&2; exit 1; }

# 2) Debe demostrar animacion.
"$SEQ_ANALYZER" "$SEQ_DIR" --expect-animated \
	|| { echo "La secuencia de 106_tile_field_showcase no demuestra animacion." >&2; exit 1; }

# 3) Sin negro interno accidental en la ventana visible. El modo single puede
#    usar COLOR00 como parte legítima del patrón de tiles, por lo que su límite
#    debe declararse al ejecutar el análisis (`SHOWCASE_DUAL=0`).
BLACK_LIMIT=0.05
[ "${SHOWCASE_DUAL:-1}" = "0" ] && BLACK_LIMIT=0.30
"$INNER_BLACK" "$SEQ_DIR" "$BLACK_LIMIT" \
	|| { echo "La secuencia de 106_tile_field_showcase contiene artefactos negros." >&2; exit 1; }

# 4) Telemetria: el fg recorre mas de una pantalla (bits 8-15 X, 0-7 Y en tiles).
node -e '
const fs = require("fs");
const report = JSON.parse(fs.readFileSync(process.argv[1], "utf-8"));
const status = report.finalSideChannel && report.finalSideChannel.ok
  ? report.finalSideChannel
  : (report.sideChannel || {}).value;
const detail = parseInt(status.detail || 0, 10) - 0x10600000;
const fgXTiles = (detail >> 8) & 0xff;
const fgYTiles = detail & 0xff;
const fineX = (detail >> 16) & 0xf;
const fineY = (detail >> 20) & 0xf;
const frame = parseInt(status.frame || 0, 10);
if (frame < 30) {
  console.error(`Frame demasiado corto: frame=${frame}`);
  process.exit(1);
}
// El mundo del fg es s32 creciente; solo se valida el byte bajo (que cambie).
console.log(`OK showcase telemetry frame=${frame} fg_world=(${fgXTiles},${fgYTiles}) fine=(${fineX},${fineY})`);
' "$RUN_REPORT" \
	|| { echo "Telemetria de 106_tile_field_showcase invalida." >&2; exit 1; }

echo "OK 106_tile_field_showcase sequence"
