#!/usr/bin/env bash
#
# Compila herramientas de scummvm-tools EN LOCAL con el toolchain del host, en vez de
# descargar los binarios oficiales de Windows (que Windows Defender marca como virus por
# falsos positivos heurísticos) o de usar el árbol completo de configure+make (que arrastra
# todas las tools y sus librerías: wx, png, zlib, freetype...).
#
# Las fuentes del oráculo viven FUERA del repositorio (por defecto
# ../../../SCUMM/scummvm-tools-2.9.0 respecto a este script, o sea ../SCUMM/... desde la raíz
# del repo). El binario se escribe junto a las fuentes (nunca en git; nada de binarios en el
# repo). Licencia: GPL-3.0 (ver COPYING en las fuentes); la herramienta se usa como proceso
# externo y nunca se enlaza en el engine (ver «Decisiones fijadas» de
# docs/guides/roadmap/ROADMAP_ADVENTURE_IMPORT.md).
#
# Uso:
#   tools/adventure/build-scummvm-tools.sh [opciones] [tool...]
#
# Opciones:
#   --src <dir>   raíz de scummvm-tools-2.9.0 (def. ../../../SCUMM/scummvm-tools-2.9.0)
#   --out <dir>   directorio del binario (def. la propia raíz de fuentes)
#   --help        esta ayuda
#
# Herramientas: descumm (decompilador de scripts SCUMM V0-V8/HE; por defecto)
#
# Entorno: CXX (def. g++), CXXFLAGS (se añaden a los flags base). En Windows la receta es la
# del toolchain del repo (WinLibs g++ del PATH de Git Bash; ver AGENTS.md §3.1).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLS_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SRC="${SCUMMVM_TOOLS_SRC:-$TOOLS_ROOT/../SCUMM/scummvm-tools-2.9.0}"
OUT=""
TOOLS=()

usage() {
	sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
	case "$1" in
	--src)
		SRC="$2"
		shift 2
		;;
	--out)
		OUT="$2"
		shift 2
		;;
	--help | -h)
		usage
		exit 0
		;;
	-*)
		echo "Opción desconocida: $1" >&2
		usage >&2
		exit 2
		;;
	*)
		TOOLS+=("$1")
		shift
		;;
	esac
done

[ -d "$SRC" ] || {
	echo "No existe la raíz de fuentes: $SRC (usa --src)" >&2
	exit 1
}
if [ "${#TOOLS[@]}" -eq 0 ]; then
	TOOLS=(descumm)
fi
if [ -z "$OUT" ]; then
	OUT="$SRC"
fi
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"

# Sufijo del ejecutable según el host (Windows/MinGW -> .exe).
EXE=""
case "$(uname -s)" in
MINGW* | MSYS* | CYGWIN*) EXE=".exe" ;;
esac

CXX="${CXX:-g++}"
# Flags base: gnu++17 fijo (el código es pre-C++20), estático para no depender de DLLs
# del toolchain en runtime (un ejecutable portable junto a las fuentes).
BASE_FLAGS=(-std=gnu++17 -O2 -I.)
LINK_FLAGS=(-static -static-libgcc -static-libstdc++)

# Objetos comunes mínimos que necesitan las tools de decompilado (lista UTILS del
# Makefile.common de scummvm-tools, sin los sound/* que arrastra el árbol completo).
COMMON_SOURCES=(
	common/file.cpp
	common/hashmap.cpp
	common/md5.cpp
	common/memorypool.cpp
	common/str.cpp
	common/stream.cpp
	common/util.cpp
)

build_descumm() {
	# descumm V0-V8/HE: descumm.cpp (V1-V5), descumm6.cpp (V6+), descumm-common.cpp
	# (helpers de bytes) y descumm-tool.cpp (CLI). No necesita zlib ni el framework
	# `decompiler/` (eso lo usan las tools de kyra/groovie).
	"$CXX" "${BASE_FLAGS[@]}" ${CXXFLAGS:-} -o "$OUT/descumm$EXE" \
		engines/scumm/descumm.cpp \
		engines/scumm/descumm6.cpp \
		engines/scumm/descumm-common.cpp \
		engines/scumm/descumm-tool.cpp \
		tool.cpp \
		version.cpp \
		"${COMMON_SOURCES[@]}" \
		"${LINK_FLAGS[@]}"
}

cd "$SRC"
for tool in "${TOOLS[@]}"; do
	echo "[build-scummvm-tools] $tool  ($SRC)"
	case "$tool" in
	descumm) build_descumm ;;
	*)
		echo "Herramienta no soportada: $tool (disponible: descumm)" >&2
		exit 2
		;;
	esac
done
echo "[build-scummvm-tools] OK -> $OUT"
