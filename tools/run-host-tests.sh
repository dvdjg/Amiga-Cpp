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
# `-Werror=narrowing`: un estrechamiento en un braced-init (p. ej. `scalar{R(v)}` de un
# fixed) suele indicar una perdida de precision no intencionada; debe romper el build.
CXXFLAGS="-std=$STD -I$ROOT/engine/include -Wall -Wextra -Werror=narrowing -O2"

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

# Comprobacion estatica del sistema de tipos (solo en la pasada completa, para no
# estorbar al iterar un test suelto). Falla si un MemoryBlock crudo se convierte.
if [ "$#" -eq 0 ]; then
	TYPE_CHECK="$ROOT/tools/check/type-tagging.mjs"
	if [ -f "$TYPE_CHECK" ] && command -v node >/dev/null 2>&1; then
		echo "== type-tagging =="
		node "$TYPE_CHECK"
	fi
	ENCODING_CHECK="$ROOT/tools/check/encoding.mjs"
	if [ -f "$ENCODING_CHECK" ] && command -v node >/dev/null 2>&1; then
		echo "== encoding =="
		node "$ENCODING_CHECK"
	fi
	LINKS_CHECK="$ROOT/tools/check/links.mjs"
	if [ -f "$LINKS_CHECK" ] && command -v node >/dev/null 2>&1; then
		echo "== links =="
		node "$LINKS_CHECK"
	fi
	MATH_DIAG="$ROOT/tools/check/math-diagnostics.sh"
	if [ -f "$MATH_DIAG" ]; then
		echo "== math-diagnostics =="
		CXX="$CXX" bash "$MATH_DIAG"
	fi
	# Codegen 68000: sin libcalls ni instrucciones de 68020 en el vocabulario de
	# fixed/MiniFloat16. Se omite si no hay toolchain cruzado.
	CODEGEN="$ROOT/tools/analyze/codegen-report.mjs"
	CODEGEN_CXX="${AMIGA_BIN_PATH:-$HOME/.vscode/extensions/bartmanabyss.amiga-debug-1.8.1/bin/win32}/opt/bin/m68k-amiga-elf-g++.exe"
	if [ -f "$CODEGEN" ] && command -v node >/dev/null 2>&1 && [ -f "$CODEGEN_CXX" ]; then
		echo "== codegen (68000) =="
		if ! node "$CODEGEN" >/dev/null; then
			echo "codegen fallo: libcalls o instrucciones de 68020 en el target." >&2
			exit 1
		fi
		echo "[codegen] OK"
	else
		echo "codegen: sin toolchain cruzado; se omite." >&2
	fi
	# La tabla funcion x escalar de SCALAR_LIBRARY.md se genera de una fuente unica.
	SCALAR_SUPPORT="$ROOT/tools/check/scalar-support.mjs"
	if [ -f "$SCALAR_SUPPORT" ] && command -v node >/dev/null 2>&1; then
		echo "== scalar-support =="
		if ! node "$SCALAR_SUPPORT"; then
			echo "scalar-support fallo: la tabla de SCALAR_LIBRARY.md no esta sincronizada." >&2
			exit 1
		fi
	fi
fi

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