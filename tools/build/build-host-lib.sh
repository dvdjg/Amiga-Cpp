#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Compila las unidades de dominio (no-header-only) del engine a una librería
# estática host, para que los tests de `tests/host/` puedan enlazarlas.
#
# Regla (docs/engine/architecture/HEADER_POLICY.md): el engine es header-only por
# defecto; SOLO las unidades no-plantilla, frías y pesadas viven en `engine/src/`
# fuera de `platform/`. Este script las compila con el g++ del host.
#
# Excluye `engine/src/platform/**` (backends de máquina, no compilables en host).
# Si no hay unidades de dominio, genera una librería vacía válida (no es error).
#
# Salida: out/host-lib/libeng.a
#
# Uso: tools/build/build-host-lib.sh [--clean]
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OBJ_DIR="$ROOT/out/host-lib/obj"
LIB="$ROOT/out/host-lib/libeng.a"
STD="gnu++23"
CXX="${CXX:-g++}"

if ! command -v "$CXX" >/dev/null 2>&1; then
	echo "ERROR: no se encontró el compilador '$CXX'." >&2
	exit 1
fi

if [ "${1:-}" = "--clean" ]; then
	rm -rf "$ROOT/out/host-lib"
fi
mkdir -p "$OBJ_DIR"

CXXFLAGS="-std=$STD -I$ROOT/engine/include -Wall -Wextra -Werror=narrowing -O2"

SOURCES="$(find "$ROOT/engine/src" -name '*.cpp' -not -path '*/platform/*' | sort || true)"

OBJECTS=()
for src in $SOURCES; do
	rel="${src#"$ROOT"/}"
	obj="$OBJ_DIR/${rel//[:\/\\]/_}.o"
	echo "  C++   $src"
	"$CXX" $CXXFLAGS -c -o "$obj" "$src"
	OBJECTS+=("$obj")
done

rm -f "$LIB"
if [ "${#OBJECTS[@]}" -gt 0 ]; then
	ar rcs "$LIB" "${OBJECTS[@]}"
	echo "[build-host-lib] $LIB ($(basename "$LIB")) con ${#OBJECTS[@]} unidad(es)."
else
	# Librería vacía válida: los tests host no necesitan enlazar nada todavía.
	printf '!<arch>\n' > "$LIB"
	echo "[build-host-lib] sin unidades de dominio; librería vacía en $LIB"
fi
