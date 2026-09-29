#!/usr/bin/env bash
# Analisis visual de la demo 204 (collide_game): jugador/obstaculo + flash de colision.
# Uso: analyze-screenshot.sh <imagen.png>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)"
exec node "$ROOT/tools/analyze/verify-204-collide-game.mjs" --image "$1"
