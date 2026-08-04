#!/usr/bin/env bash
# Secuencia de verificacion temporal de esta demo (estabilidad + pixel).
# Uso: analyze-sequence.sh [--warp] [--pixel-assert] [--require-pixel-assert-ok]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="demos/051_blitter_shifted_bobs"
RUN="$ROOT/tools/run/run-demo.sh"
SEQ_ANALYZER="$ROOT/tools/analyze/analyze-frame-sequence.sh"
PIXEL_ASSERT="$ROOT/tools/analyze/assert-pixel-contract.sh"
PIXEL_CONTRACT="$(dirname "${BASH_SOURCE[0]}")/pixel-contract.json"
SEQ_DIR="$ROOT/out/run/051_blitter_shifted_bobs/sequence"
PIXEL_OUT="$ROOT/out/analysis/051_blitter_shifted_bobs/pixel-assert"

WARP=0; PA=0; REQUIRE_PA=0
for arg in "$@"; do
  case "$arg" in
    --warp) WARP=1 ;;
    --pixel-assert) PA=1 ;;
    --require-pixel-assert-ok) PA=1; REQUIRE_PA=1 ;;
  esac
done

run_capture() {
  for attempt in 1 2; do
    local extra=()
    [ "$WARP" -eq 1 ] && extra+=(--warp)
    if "$RUN" "$DEMO" --settle-ms 1200 --sequence-frames 6 --sequence-interval-ms 120 "${extra[@]}"; then
      return 0
    fi
    [ "$attempt" -eq 1 ] && sleep 2
  done
  echo "No se pudo capturar la secuencia de 051_blitter_shifted_bobs." >&2
  exit 1
}

run_capture
if ! "$SEQ_ANALYZER" "$SEQ_DIR" --expect-static; then
  echo "La secuencia de 051_blitter_shifted_bobs no es estable tras el frame validado." >&2
  exit 1
fi
if [ "$PA" -eq 1 ]; then
  if ! "$PIXEL_ASSERT" --sequence-dir "$SEQ_DIR" --contract "$PIXEL_CONTRACT" --out-dir "$PIXEL_OUT"; then
    echo "Pixel Assertions detecto desviacion." >&2
    exit 1
  fi
fi
echo "OK 051_blitter_shifted_bobs sequence"
