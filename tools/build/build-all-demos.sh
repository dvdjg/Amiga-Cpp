#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Gate "compilar la flota" (DT-010): compila TODAS las demos (solo compilar; NO
# ejecuta) para cazar deriva de API acumulada. La regresión de demos ejecuta las
# que prueba; esto cierra el caso "una demo dejó de compilar y nadie lo vio".
#
# Uso:
#   bash tools/build/build-all-demos.sh [-j N] [--release]
#
#   -j N        builds en paralelo (defecto 4; el contador de versión es atómico).
#   --release   compila en modo release (por defecto debug, como la regresión).
#
# Salida: una línea por demo (OK/FAIL) + resumen; los errores de las fallidas quedan
# en out/build-all-demos/<demo>.log. Exit != 0 si alguna falla.
# ---------------------------------------------------------------------------
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT" || exit 1

JOBS=4
MODE="--debug"
while [ "$#" -gt 0 ]; do
	case "$1" in
		-j)
			shift
			JOBS="${1:-4}"
			;;
		-j*) JOBS="${1#-j}" ;;
		--release) MODE="--release" ;;
		--debug) MODE="--debug" ;;
		*) echo "Uso: build-all-demos.sh [-j N] [--release]" >&2; exit 2 ;;
	esac
	shift
done

LOGDIR="$ROOT/out/build-all-demos"
mkdir -p "$LOGDIR"

# Lista de demos (una por carpeta con src/main.cpp).
mapfile -t DEMOS < <(find demos -path '*/src/main.cpp' | sed 's|/src/main.cpp$||' | sort)
if [ "${#DEMOS[@]}" -eq 0 ]; then
	echo "[build-all] no se encontraron demos" >&2
	exit 1
fi
echo "[build-all] ${#DEMOS[@]} demos, modo $MODE, paralelo=$JOBS"

build_one() {
	local demo="$1"
	local out
	out=$(bash ./tools/build/build-demo.sh "$demo" "$MODE" 2>&1)
	if echo "$out" | grep -qE '^  OK'; then
		echo "OK   $demo"
	else
		echo "$out" > "$LOGDIR/$(echo "$demo" | tr '/' '_').log"
		echo "FAIL $demo"
	fi
}

run_limited() {
	local i=0
	for demo in "${DEMOS[@]}"; do
		build_one "$demo" &
		i=$((i + 1))
		if [ $((i % JOBS)) -eq 0 ]; then
			wait
		fi
	done
	wait
}

run_limited | tee "$LOGDIR/summary.txt"
FAILS=$(grep -c '^FAIL' "$LOGDIR/summary.txt" || true)
OKS=$(grep -c '^OK' "$LOGDIR/summary.txt" || true)
echo "[build-all] $OKS OK, $FAILS FAIL (logs de fallos en out/build-all-demos/)"
[ "$FAILS" -eq 0 ]
