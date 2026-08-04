#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Cliente de linea de comandos del canal lateral de depuracion WinUAE-DBG.
# Sustituye al antiguo winuae-side-channel.ps1 (multiplataforma).
#
# Uso: tools/debug/winuae-side-channel.sh <comando> [args...] [--port 2346]
# Ejemplos: state | regs | mem <addr> <len> | poke <addr> <hex> [label] |
#           screenshot <ruta> | profile <frames> <out> | pause | resume ...
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "$#" -lt 1 ]; then
	exec node "$ROOT/dist/tools/debug/winuae-side-channel.js"
fi

exec node "$ROOT/dist/tools/debug/winuae-side-channel.js" "$@"
