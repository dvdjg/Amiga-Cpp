#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verifica el contrato del canal lateral: READY, locks, input, screenshot y
# profile sobre una demo. Sustituye al antiguo verify-side-channel-contract.ps1.
# Uso: tools/debug/verify-side-channel-contract.sh --demo <demo> [opciones]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

exec node "$ROOT/dist/tools/debug/verify-side-channel-contract.js" "$@"
