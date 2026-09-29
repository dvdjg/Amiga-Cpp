#!/usr/bin/env bash
# Gate visual temporal de la demo 204: captura una secuencia y verifica que el jugador
# se mueve y que la colision (flash) se detecta. Lo invoca tools/test-regression.sh.
#
# Uso: demos/techniques/amiga/blitter/204_collide_game/analyze-sequence.sh [--warp] [...]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
DEMO="demos/techniques/amiga/blitter/204_collide_game"

RUN_ARGS=("$ROOT/tools/run/run-demo.sh" "$DEMO" --sequence-frames 16 --sequence-interval-ms 120)
for a in "$@"; do
	[ "$a" = "--warp" ] && RUN_ARGS+=("--warp")
done
bash "${RUN_ARGS[@]}"

SEQ="$(find "$ROOT/out/run/204_collide_game" -maxdepth 2 -type d -name sequence 2>/dev/null | head -1)"
if [ -z "$SEQ" ]; then
	echo "No se encontro la secuencia de la demo 204" >&2
	exit 1
fi

exec node "$ROOT/tools/analyze/verify-204-collide-game.mjs" --sequence-dir "$SEQ"
