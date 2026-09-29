#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 301 (layouts, texto ajustado y fuentes).
#
# Comprueba, sobre la paleta EHB, que la UI con layouts se ha pintado: relleno de
# paneles, texto repartido por la pantalla (>=3 cuadrantes) y bisel claro. No exige
# overlay ni animacion (la demo es estatica).
#
# Uso: demos/features/ui/amiga/301_gui_layouts/analyze-screenshot.sh <imagen.png>
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

exec node "$ROOT/tools/analyze/verify-gui-layouts.mjs" --image "$IMAGE"
