#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 078 (solido 3D relleno con math3d+mesh3d+Blitter).
# Sustituye al analizador generico (no hay overlay de texto): comprueba fondo,
# marco fijo y pixeles del solido con tools/analyze/verify-math3d-cube.mjs.
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

exec node "$ROOT/tools/analyze/verify-math3d-cube.mjs" --image "$IMAGE"
