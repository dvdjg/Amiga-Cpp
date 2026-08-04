#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verifica la escritura reversible de memoria (poke -> audit -> rollback) por
# el canal lateral con lock takeover. Sustituye al antiguo
# verify-side-channel-takeover.ps1.
# Uso: tools/debug/verify-side-channel-takeover.sh --demo <demo> [opciones]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

exec node "$ROOT/dist/tools/debug/verify-side-channel-takeover.js" "$@"
