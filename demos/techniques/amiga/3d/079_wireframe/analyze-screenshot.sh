#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 079 (wireframe `pilka` por linea de Blitter).
#
# Sustituye al analizador generico (que pide colores de overlay blanco/verde/amarillo)
# porque esta demo usa una paleta azul/cian: se comprueba fondo oscuro + alambre con
# tools/analyze/verify-wireframe.mjs.
#
# Uso: demos/techniques/amiga/3d/079_wireframe/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/tools/analyze/verify-wireframe.mjs" --image "$IMAGE"
