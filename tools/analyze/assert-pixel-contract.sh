#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Ejecuta las aserciones de pixeles declaradas en un pixel-contract.json.
# Sustituye a assert-pixel-contract.ps1. Delega en assert-pixel-contract.js.
# Uso: tools/analyze/assert-pixel-contract.sh --sequence-dir <dir>
#      --contract <pixel-contract.json> [--run-report <run-report.json>]
#      [--out-dir <dir>]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/assert_pixel_contract.js" "$@"
