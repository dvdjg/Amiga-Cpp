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
# Uso: tools/run-host-tests.sh [--category <cat>] [tests/host/<cat>/NNN_nombre ...]
#   Sin argumentos: compila y corre TODOS los tests/host.
#   Con rutas: solo esos tests.
#   Con --category: solo los tests de esa categoría (p. ej. core, graphics, platform/amiga).
#   Ver docs/testing/TAXONOMY.md.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/out/host-tests"
STD="gnu++23"
# `-Werror=narrowing`: un estrechamiento en un braced-init (p. ej. `scalar{R(v)}` de un
# fixed) suele indicar una perdida de precision no intencionada; debe romper el build.
CXXFLAGS="-std=$STD -I$ROOT/engine/include -Wall -Wextra -Werror=narrowing -O2"

CXX="${CXX:-g++}"

# --- Argumentos -------------------------------------------------------------
# Sin argumentos: todos los tests host. Con rutas: solo esos. Con `--category X`:
# solo los tests de la categoría X (`tests/host/X/...`). Ver docs/testing/TAXONOMY.md.
CATEGORY=""
ARGS=()
while [ "$#" -gt 0 ]; do
	case "$1" in
		--category) CATEGORY="${2:-}"; shift 2 ;;
		--category=*) CATEGORY="${1#*=}"; shift ;;
		*) ARGS+=("$1"); shift ;;
	esac
done

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
if [ "${#ARGS[@]}" -eq 0 ] && [ -z "$CATEGORY" ]; then
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
	# Numeracion de los tests host (sin duplicados y catalogo sincronizado).
	TEST_NUMBERING="$ROOT/tools/check/test-numbering.mjs"
	if [ -f "$TEST_NUMBERING" ] && command -v node >/dev/null 2>&1; then
		echo "== test-numbering =="
		if ! node "$TEST_NUMBERING"; then
			echo "test-numbering fallo: numeros de test duplicados o catalogo desincronizado." >&2
			exit 1
		fi
	fi
	# Numeracion de las demos (sin duplicados por plataforma; ver docs/ai-dev-environment/NUMBERING.md).
	DEMO_NUMBERING="$ROOT/tools/check/demo-numbering.mjs"
	if [ -f "$DEMO_NUMBERING" ] && command -v node >/dev/null 2>&1; then
		echo "== demo-numbering =="
		if ! node "$DEMO_NUMBERING"; then
			echo "demo-numbering fallo: numeros de demo duplicados." >&2
			exit 1
		fi
	fi
	# Regla de oro: cabeceras genericas sin escalares concretos (fixed/minifloat/q*).
	GENERIC_HEADERS="$ROOT/tools/check/generic-headers.mjs"
	if [ -f "$GENERIC_HEADERS" ] && command -v node >/dev/null 2>&1; then
		echo "== generic-headers =="
		if ! node "$GENERIC_HEADERS"; then
			echo "generic-headers fallo: tipo concreto en cabecera generica." >&2
			exit 1
		fi
	fi
	# Frontera dominio <-> plataforma (anillo 0 no toca vocabulario de chipset).
	PLATFORM_BOUNDARIES="$ROOT/tools/check/platform-boundaries.mjs"
	if [ -f "$PLATFORM_BOUNDARIES" ] && command -v node >/dev/null 2>&1; then
		echo "== platform-boundaries =="
		if ! node "$PLATFORM_BOUNDARIES"; then
			echo "platform-boundaries fallo: el dominio incluye vocabulario de plataforma." >&2
			exit 1
		fi
	fi
	# Features: demos portables sin hardware directo (registros ni vocabulario de chipset).
	DEMO_PLATFORM_BOUNDARIES="$ROOT/tools/check/demo-platform-boundaries.mjs"
	if [ -f "$DEMO_PLATFORM_BOUNDARIES" ] && command -v node >/dev/null 2>&1; then
		echo "== demo-platform-boundaries =="
		if ! node "$DEMO_PLATFORM_BOUNDARIES"; then
			echo "demo-platform-boundaries fallo: una feature usa hardware directo." >&2
			exit 1
		fi
	fi
	# Estructura tematica del engine (eng/ y core/ por tema; familias de backend).
	ENGINE_TREE="$ROOT/tools/check/engine-tree.mjs"
	if [ -f "$ENGINE_TREE" ] && command -v node >/dev/null 2>&1; then
		echo "== engine-tree =="
		if ! node "$ENGINE_TREE"; then
			echo "engine-tree fallo: estructura tematica del engine fuera de canon." >&2
			exit 1
		fi
	fi
	# Politica de cabeceras: cabeceras grandes y funciones no-inline (estricto; baseline vacio).
	HEADER_IMPL="$ROOT/tools/check/header-impl.mjs"
	if [ -f "$HEADER_IMPL" ] && command -v node >/dev/null 2>&1; then
		echo "== header-impl =="
		if ! node "$HEADER_IMPL" --strict; then
			echo "header-impl fallo: cabecera grande o funcion no-inline fuera de baseline." >&2
			exit 1
		fi
	fi
	# No-propietarios: los punteros a objeto en miembros deben ser eng::Ref/NonNull.
	RAW_PTR="$ROOT/tools/check/raw-pointer-members.mjs"
	if [ -f "$RAW_PTR" ] && command -v node >/dev/null 2>&1; then
		echo "== raw-pointer-members =="
		if ! node "$RAW_PTR"; then
			echo "raw-pointer-members fallo: miembro con puntero a objeto no propietario." >&2
			exit 1
		fi
	fi
	# Documentacion: toda funcion nueva debe llevar comentario descriptivo (baseline aparte).
	DOC_COV="$ROOT/tools/check/doc-coverage.mjs"
	if [ -f "$DOC_COV" ] && command -v node >/dev/null 2>&1; then
		echo "== doc-coverage =="
		if ! node "$DOC_COV"; then
			echo "doc-coverage fallo: funcion nueva sin comentario descriptivo." >&2
			exit 1
		fi
	fi
	# Docs de hallazgos: indexados en su README y con nombre kebab-case (AGENTS.md §1.3).
	DOC_INDEX="$ROOT/tools/check/doc-index.mjs"
	if [ -f "$DOC_INDEX" ] && command -v node >/dev/null 2>&1; then
		echo "== doc-index =="
		if ! node "$DOC_INDEX"; then
			echo "doc-index fallo: doc de hallazgo huerfano o con nombre fuera de la convencion." >&2
			exit 1
		fi
	fi
	# Arquitectura: cabeceras fundamentales con diagrama ASCII (estricto).
	DIAGRAMS="$ROOT/tools/check/architecture-diagrams.mjs"
	if [ -f "$DIAGRAMS" ] && command -v node >/dev/null 2>&1; then
		echo "== architecture-diagrams =="
		if ! node "$DIAGRAMS" --strict; then
			echo "architecture-diagrams fallo: cabecera fundamental sin diagrama ASCII." >&2
			exit 1
		fi
	fi
	# Detector temporal de parpadeo: auto-test con secuencias sintéticas (se omite si no hay
	# OpenCV/Python; exit 3 = omitido, no falla).
	ST_TEMPORAL="$ROOT/tools/vision-review/selftest-temporal.mjs"
	if [ -f "$ST_TEMPORAL" ] && command -v node >/dev/null 2>&1; then
		echo "== selftest-temporal =="
		node "$ST_TEMPORAL"; ec=$?
		if [ "$ec" -ne 0 ] && [ "$ec" -ne 3 ]; then
			echo "selftest-temporal fallo: el detector de parpadeo no distingue glitch de movimiento." >&2
			exit 1
		fi
	fi
fi

# Selección de tests.
if [ "${#ARGS[@]}" -gt 0 ]; then
	for arg in "${ARGS[@]}"; do
		run_test "$arg"
	done
elif [ -n "$CATEGORY" ]; then
	CAT_DIR="$ROOT/tests/host/$CATEGORY"
	if [ ! -d "$CAT_DIR" ]; then
		echo "ERROR: no existe la categoría '$CATEGORY' ($CAT_DIR)." >&2
		exit 1
	fi
	while IFS= read -r test_dir; do
		run_test "$test_dir"
	done < <(find "$CAT_DIR" -type d -name '[0-9][0-9][0-9]_*' | sort)
else
	while IFS= read -r test_dir; do
		run_test "$test_dir"
	done < <(find "$ROOT/tests/host" -type d -name '[0-9][0-9][0-9]_*' | sort)
	# Regresion de nivel de los naipes: barrido determinista de selfplay contra la
	# linea base congelada. Solo en la pasada completa (necesita g++ y node).
	CARDS_REGRESSION="$ROOT/tools/cards/regression.sh"
	if [ -f "$CARDS_REGRESSION" ]; then
		echo "== cards regression =="
		if ! CXX="$CXX" bash "$CARDS_REGRESSION"; then
			echo "cards regression fallo: el nivel de eng::cards cambio (revisar/--update)" >&2
			exit 1
		fi
	fi
fi

echo "Host tests finalizados."