#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verificacion visual de la demo 302 (HUD de juego en franja propia).
#
# Sustituye al analizador generico: comprueba la franja del HUD (texto blanco,
# relleno amarillo y, si hay secuencia, animacion) con
# tools/analyze/verify-hud-band.mjs.
#
# Uso: demos/features/ui/amiga/302_hud_band/analyze-screenshot.sh <imagen.png>
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
	exec node "$ROOT/tools/analyze/verify-hud-band.mjs" --sequence-dir "$SEQD"
fi
exec node "$ROOT/tools/analyze/verify-hud-band.mjs" --image "$IMAGE"
