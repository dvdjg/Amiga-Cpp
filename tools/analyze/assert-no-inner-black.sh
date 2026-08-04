#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Detecta negro interno inesperado en capturas o secuencias de frames.
# Sustituye a assert-no-inner-black.ps1. Delega en assert_no_inner_black.py.
# Uso: tools/analyze/assert-no-inner-black.sh <archivo|carpeta> [maxBlackRatio]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$ROOT/tools/analyze/assert_no_inner_black.py" "$@"
