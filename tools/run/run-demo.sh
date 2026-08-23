#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Ejecuta una demo/test en WinUAE-DBG y captura, esperando READY por el canal
# lateral (g_eng_run_status). Sustituye al antiguo run-demo.ps1 (multiplataforma).
#
# Uso: tools/run/run-demo.sh <demo|test> [--wait-ms N] [--warp] [--keep-running]
#       [--sequence-frames N] [--sequence-interval-ms N] [--mouse-from X1,Y1
#       --mouse-to X2,Y2] [--screenshot ruta] [--protect target,block|set:0xVALUE,size] ...
# --protect se puede repetir; target es un simbolo del .map (se reloca tras
# READY por canal lateral) o una direccion hex 0x... . Usa WinUAE-DBG v2.1.
# Ver docs/build/BUILD_AND_RUN.md para la lista completa de opciones.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "$#" -lt 1 ]; then
	echo "Uso: tools/run/run-demo.sh <demo|test> [opciones de run-demo.ts]" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/run/run-demo.js" "$@"
