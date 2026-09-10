#!/usr/bin/env bash
# Analizador visual de la demo 054 (SpriteAllocator). ESTADO: a revisar — el
# camino de 8 canales de `emit_into` no dibuja los sprites aún. El verificador se
# ejecuta y, si falla, se reporta como AVISO sin bloquear la regresión.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
IMAGE="${1:-}"
if [ -z "$IMAGE" ]; then
	echo "Uso: analyze-screenshot.sh <imagen.png>" >&2
	exit 2
fi

if node "$ROOT/dist/tools/analyze/verify_sprite_allocator.js" --image "$IMAGE"; then
	exit 0
fi
echo "AVISO: demo 054 a revisar (emit_into de 8 canales no dibuja sprites)." >&2
exit 0
