#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Selftest del motor de aserciones de pixeles (assert-pixel-contract.js).
# Sustituye a verify-pixel-assert.ps1. Delega en verify_pixel_assert.js.
# Uso: tools/analyze/verify-pixel-assert.sh [--out-dir <dir>]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
exec node "$ROOT/dist/tools/analyze/verify_pixel_assert.js" "$@"
