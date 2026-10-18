#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate visual de la demo 080 (paneo de camara con frames intermedios).
#
# Captura una secuencia de display y verifica el **contrato de pixel del paneo**
# con el verificador adaptativo (`verify-080-pan-window.mjs`): invariantes de la
# politica Full/Fake y, por cada offset reportado, que el Fake es el Full anterior
# desplazado (con la costura de captura acotada). Lo invoca tools/test-regression.sh
# (reenvia --warp).
#
# Uso: demos/techniques/amiga/3d/080_camera_pan_window/analyze-sequence.sh [--warp]
#
# La captura por paso esta BLOQUEADA por DT-033 (`--sequence-step-frames` aborta con
# «GDB socket is closed ... dropped after a timeout» en captureScreenshot). Mientras
# tanto se captura por intervalo (frames de display reales, con el banding del Full
# mid-render): la secuencia de contrato con frames nitidos queda pendiente.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/3d/080_camera_pan_window"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 16 --sequence-interval-ms 250)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run/080_camera_pan_window" -maxdepth 2 -type d -name sequence 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 080" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-080-pan-window.mjs" --sequence-dir "$SEQ"
