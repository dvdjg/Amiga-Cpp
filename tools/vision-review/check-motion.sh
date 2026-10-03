#!/usr/bin/env bash
# Captura una secuencia de una demo de scroll y valida el MOVIMIENTO horizontal
# (`motion-check.py`): no congelado, dirección correcta y sin saltos enormes.
#
# Complementa la validación con visión (Ollama, regla de oro) sobre la misma secuencia.
#
# Uso: tools/vision-review/check-motion.sh <demo_dir> [config]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEMO="${1:?Uso: check-motion.sh <demo_dir> [config]}"
CFG="${2:-A500_release}"
TRACK="${TRACK:-255,255,255}"
ID="$(basename "$DEMO")"
bash "$ROOT/tools/run/run-demo.sh" "$DEMO" --config "$CFG" --sequence-frames 6 \
	--sequence-interval-ms 150 >/dev/null 2>&1
SEQ="$ROOT/out/run/$ID/$CFG/sequence"
# Marcador unico (blanco) si la demo lo tiene; si no, modo SSD.
if [ "${TRACK:-255,255,255}" = "0" ]; then
    python "$ROOT/tools/vision-review/motion-check.py" "$SEQ"
else
    python "$ROOT/tools/vision-review/motion-check.py" "$SEQ" --track "$TRACK"
fi
