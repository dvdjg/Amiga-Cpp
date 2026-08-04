#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Verifica la coexistencia del canal lateral con una sesion GDB activa
# (breakpoint en eng_debug_ready_probe, step y muestreo de estado).
# Sustituye al antiguo verify-gdb-step-side-channel.ps1.
# Uso: tools/debug/verify-gdb-step-side-channel.sh --demo <demo> [--steps N]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

exec node "$ROOT/dist/tools/debug/verify-gdb-step-side-channel.js" "$@"
