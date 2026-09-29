#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 077 (cubo 3D math3d+mesh3d).
#
# Sustituye al analizador generico (analyze-screenshot.sh) porque esta demo no
# dibuja overlay de texto: se comprueba la paleta real de la escena (fondo, marco
# fijo y pixeles del cubo) con tools/analyze/verify-math3d-cube.mjs.
#
# Si existe la secuencia de capturas (run-demo.sh --sequence-frames N), se analiza
# la secuencia: el cubo puede quedar de canto en una captura suelta (0 px) y el
# modo secuencia exige 3/4 frames con cubo + animacion, de forma determinista.
#
# Uso: demos/techniques/amiga/3d/077_math3d_cube/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

SEQD="$(dirname "$IMAGE")/sequence"
if [ -d "$SEQD" ]; then
	exec node "$ROOT/tools/analyze/verify-math3d-cube.mjs" --sequence-dir "$SEQD"
fi
exec node "$ROOT/tools/analyze/verify-math3d-cube.mjs" --image "$IMAGE"
