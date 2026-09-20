#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 078 (solido 3D relleno con math3d+mesh3d+Blitter).
# Sustituye al analizador generico (no hay overlay de texto): comprueba fondo,
# marco fijo y pixeles del solido con tools/analyze/verify-math3d-cube.mjs.
#
# Si existe la secuencia de capturas (run-demo.sh --sequence-frames N), se analiza
# la secuencia: el solido puede quedar de canto en una captura suelta (0 px) y el
# modo secuencia exige 3/4 frames con cubo + animacion, de forma determinista.
#
# Uso: demos/amiga/078_math3d_solid/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
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
