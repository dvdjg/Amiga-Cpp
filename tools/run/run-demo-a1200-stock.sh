#!/usr/bin/env bash
# A1200 stock: ejecuta el binario 68000 A500_release sobre A1200/AGA/KS 3.1.
# Decision 2026-10-20: 49,68 fps medidos; el A1200 nativo 68020 (47,94 fps stock)
# se reserva para A1200 con Fast RAM/accelerador. No alcanza 50 fps en stock.
# Aislamiento: usa el runner A1200 (GDB 2445 / lateral 2446); no toca 2345/2346.
#
# Uso:
#   ./tools/run/run-demo-a1200-stock.sh dk_port_xl --wait-ms 5000 --settle-ms 11000
# (deja telemetry-samples=0 por defecto; el muestreo por GDB puede agotar timeout
#  con el servidor no intrusivo; para fps usar el canal lateral, no readMemory).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

DEMO="${1:-dk_port_xl}"
if [ $# -gt 0 ]; then
	shift
fi

exec "$ROOT/tools/run/run-demo-a1200.sh" "$DEMO" --config A500_release "$@"
