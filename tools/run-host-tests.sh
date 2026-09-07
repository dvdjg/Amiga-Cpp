#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Compila y ejecuta los tests HOST del engine (algoritmos/APIs puras).
#
# Estos tests viven en tests/host/NNN_<nombre>/src/main.cpp, compilan contra
# engine/include con g++ del HOST (no con el cruce Amiga ni con MSVC) y corren
# como binario nativo: son la forma más rápida de validar matemáticas,
# ordenación y otras APIs freestanding sin abrir WinUAE.
#
# Compilador: usa `g++` del PATH del entorno de desarrollo (el mismo GCC del
# toolchain del proyecto, p. ej. el que resuelve `AMIGA_BIN_PATH` o la
# extensión Bartman). No hay dependencia de WSL.
#
# Uso: tools/run-host-tests.sh [tests/host/NNN_nombre ...]
#   Sin argumentos: compila y corre TODOS los tests/host.
#   Con rutas: solo esos tests.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/out/host-tests"
STD="gnu++23"
CXXFLAGS="-std=$STD -I$ROOT/engine/include -Wall -Wextra -O2"

CXX="${CXX:-g++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
	echo "ERROR: no se encontró el compilador '$CXX'." >&2
	echo "Los tests host necesitan el g++ del entorno de desarrollo." >&2
	exit 1
fi

mkdir -p "$BUILD_DIR"

run_test() {
	local test_dir="$1"
	local name
	name="$(basename -- "$test_dir")"
	local src="$test_dir/src/main.cpp"
	local bin="$BUILD_DIR/$name"

	if [ ! -f "$src" ]; then
		echo "AVISO: sin src/main.cpp en $test_dir; se omite."
		return
	fi

	echo "==> [$name]"
	"$CXX" $CXXFLAGS "$src" -o "$bin"
	"$bin"
	echo ""
}

# Selección de tests.
if [ "$#" -gt 0 ]; then
	for arg in "$@"; do
		run_test "$arg"
	done
else
	for test_dir in "$ROOT"/tests/host/*/; do
		[ -d "$test_dir" ] || continue
		run_test "${test_dir%/}"
	done
fi

echo "Host tests finalizados."