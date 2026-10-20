#!/usr/bin/env bash
# Runner A1200/AGA aislado: no toca las instancias A500 (2345/2346) ni default.uae.
# Usa puertos propios (2445/2446) y lanzamiento legacy con -f <runner.uae>, de modo
# que puede convivir con WinUAE A500 ya en marcha.
#
# Perfiles:
#   - A1200_release: 68020 nativo (A1200 con Fast RAM/accelerador; 47,94 fps stock).
#   - A500_release:  binario 68000 sobre A1200 stock (49,68 fps); usar
#                    tools/run/run-demo-a1200-stock.sh o --config A500_release.
#
# Uso:
#   ./tools/run/run-demo-a1200.sh dk_port_xl --config A1200_release \
#       --wait-ms 5000 --settle-ms 11000
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# No heredar configuracion de una sesion A500 (quickstart, cwd, exe, triggers...).
unset WINUAE_CWD WINUAE_EXE WINUAE_DEBUGGING_TRIGGER
unset WINUAE_NO_CYCLE_EXACT WINUAE_HEADLESS
unset WINUAE_SESSION_IDLE_TIMEOUT_MS WINUAE_SESSION_IDLE_ACTION

export WINUAE_QUICKSTART="${A1200_QUICKSTART:-a1200,0}"
export WINUAE_KICKSTART="${A1200_KICKSTART:-C:/Amiga/KICK_A1200.rom}"
# `-f <runner.uae>` + `-s debugging_features=gdbserver`; no escribe default.uae.
export WINUAE_USE_LEGACY_LAUNCH="${A1200_USE_LEGACY_LAUNCH:-1}"
# Puertos distintos de los de A500. No heredar WINUAE_GDB_PORT=2345 de una
# instancia A500: los overrides propios se llaman A1200_GDB_PORT/A1200_SIDE_PORT.
export WINUAE_GDB_PORT="${A1200_GDB_PORT:-2445}"
export WINUAE_SIDE_CHANNEL_PORT="${A1200_SIDE_PORT:-2446}"
export RUN_DEMO_STRICT_READY=1
SIDE_PORT="$WINUAE_SIDE_CHANNEL_PORT"
GDB_PORT="$WINUAE_GDB_PORT"

# Si los puertos propios estan ocupados, avisa; no mata nada.
if command -v netstat >/dev/null 2>&1; then
	if netstat -ano 2>/dev/null | grep -qE "[:.](${GDB_PORT}|${SIDE_PORT})[[:space:]]"; then
		echo "run-demo-a1200: aviso: ${GDB_PORT}/${SIDE_PORT} ya estan en uso." >&2
		echo "  Cierra solo esa instancia A1200 o usa WINUAE_GDB_PORT/WINUAE_SIDE_CHANNEL_PORT." >&2
	fi
fi

echo "[run-demo-a1200] GDB=${GDB_PORT} side=${SIDE_PORT} quickstart=${WINUAE_QUICKSTART}" >&2
exec "$ROOT/tools/run/run-demo.sh" "$@" --side-channel-port "${SIDE_PORT}"
