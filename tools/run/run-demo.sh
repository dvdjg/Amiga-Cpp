#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Ejecuta una demo/test en WinUAE-DBG y captura, esperando READY por el canal
# lateral (g_eng_run_status). Sustituye al antiguo run-demo.ps1 (multiplataforma).
#
# Uso: tools/run/run-demo.sh <demo|test> [--wait-ms N] [--warp] [--keep-running]
#       [--sequence-frames N] [--sequence-interval-ms N] [--mouse-from X1,Y1
#       --mouse-to X2,Y2] [--screenshot ruta] ...
# Ver docs/build/BUILD_AND_RUN.md para la lista completa de opciones.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

if [ "$#" -lt 1 ]; then
	echo "Uso: tools/run/run-demo.sh <demo|test> [opciones de run-demo.ts]" >&2
	exit 2
fi

exec node "$ROOT/dist/tools/run/run-demo.js" "$@"
