#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Detecta negro interno inesperado en capturas o secuencias de frames.
# Sustituye a assert-no-inner-black.ps1. Delega en assert_no_inner_black.js.
# Uso: tools/analyze/assert-no-inner-black.sh <archivo|carpeta> [maxBlackRatio]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/assert_no_inner_black.js" "$@"
