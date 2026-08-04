#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verifica pause/resume bajo lock takeover y lectura de memoria mientras la
# CPU esta pausada. Sustituye al antiguo verify-side-channel-pause-resume.ps1.
# Uso: tools/debug/verify-side-channel-pause-resume.sh --demo <demo> [opciones]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

exec node "$ROOT/dist/tools/debug/verify-side-channel-pause-resume.js" "$@"
