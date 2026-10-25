#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Regresión on-target: descubre tests/amiga/*/*/verify.sh y los ejecuta.
#
# Uso:
#   tools/test-on-target.sh [--skip-build] [--no-watchdog] [--test <ruta>]... [--list]
#
# --skip-build reutiliza los binarios ya compilados.
# --test limita la ejecución a una ruta concreta (repetible).
# --list muestra los tests descubiertos sin ejecutarlos.
# ---------------------------------------------------------------------------
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SKIP_BUILD=0
LIST_ONLY=0
WATCHDOG=1
TESTS=()

while [ "$#" -gt 0 ]; do
	case "$1" in
		--skip-build) SKIP_BUILD=1; shift ;;
		--no-watchdog) WATCHDOG=0; shift ;;
		--list) LIST_ONLY=1; shift ;;
		--test)
			if [ -z "${2:-}" ]; then
				echo "Falta la ruta de --test" >&2
				exit 2
			fi
			TESTS+=("$2")
			shift 2
			;;
		*)
			echo "Argumento desconocido: $1" >&2
			exit 2
			;;
	esac
done

if [ "${#TESTS[@]}" -eq 0 ]; then
	mapfile -t TESTS < <(find "$ROOT/tests/amiga" -name verify.sh -print | sort)
fi

if [ "${#TESTS[@]}" -eq 0 ]; then
	echo "No se encontraron tests on-target." >&2
	exit 1
fi

if [ "$LIST_ONLY" -eq 1 ]; then
	printf '%s\n' "${TESTS[@]}"
	exit 0
fi

if [ "$WATCHDOG" -eq 1 ]; then
	export ENG_RUN_WATCHDOG=1
fi

FAILED=0
for test in "${TESTS[@]}"; do
	if [ ! -f "$test" ]; then
		echo "AVISO: no existe $test" >&2
		FAILED=1
		continue
	fi
	echo "==> [$test]"
	args=()
	if [ "$SKIP_BUILD" -eq 1 ]; then
		args+=("--skip-build")
	fi
	if bash "$test" "${args[@]}"; then
		continue
	fi
	echo "FALLO: $test" >&2
	FAILED=1
done

if [ "$FAILED" -ne 0 ]; then
	echo "Regresión on-target: FALLOS" >&2
	exit 1
fi
echo "Regresión on-target: OK"
