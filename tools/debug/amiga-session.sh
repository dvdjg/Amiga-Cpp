#!/usr/bin/env bash
# Consulta la sesión actual publicada por build-current-demo.sh y el canal lateral.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SESSION="$ROOT/out/debug-current/session.json"
COMMAND="${1:-info}"
shift || true

case "$COMMAND" in
	info)
		if [ -f "$SESSION" ]; then
			cat "$SESSION"
		else
			echo "No hay sesión compilada en $SESSION" >&2
			exit 1
		fi
		;;
	state|regs|pause|resume|lock|mem|screenshot|profile|profile-status|action)
		exec node "$ROOT/dist/tools/debug/winuae-side-channel.js" "$COMMAND" "$@"
		;;
	*)
		echo "Uso: tools/debug/amiga-session.sh info|state|regs|pause|resume|lock|mem|screenshot|profile|profile-status|action [args...]" >&2
		exit 2
		;;
esac
