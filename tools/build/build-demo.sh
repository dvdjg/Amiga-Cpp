#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# Compila una demo/test del engine para Amiga. Sustituye a build-demo.ps1.
#
# Uso: tools/build/build-demo.sh <demo|test> [--debug|--release] [--clean]
#
# El toolchain se resuelve en este orden:
#   1. $AMIGA_BIN_PATH (variable de entorno, forma portable en Linux/macOS).
#   2. Extensiones de Cursor/VS Code (amiga-debug) en Windows, como fallback.
#   3. Binarios `m68k-amiga-elf-*` en el PATH (toolchain instalado por separado).
#
# Salida en out/demos/<leaf>/{<leaf>.elf,.exe,.map,.s}.
# ---------------------------------------------------------------------------
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# --- Argumentos -------------------------------------------------------------
DEMO="${1:-}"
DEBUG_BUILD=0
CLEAN=0
O0_BUILD=0
for arg in "$@"; do
	case "$arg" in
		--debug) DEBUG_BUILD=1 ;;
		--release) DEBUG_BUILD=0 ;;
		--o0) O0_BUILD=1 ;;
		--clean) CLEAN=1 ;;
		-*)
			if [ "$arg" = "$DEMO" ]; then :; else :; fi
			;;
	esac
done
if [ -z "$DEMO" ]; then
	echo "Uso: tools/build/build-demo.sh <demo|test> [--debug|--release] [--clean]" >&2
	exit 2
fi
if [[ "$DEMO" == -* ]]; then
	DEMO="${2:-}"
fi

DEMO_PATH="$ROOT/$DEMO"
if [ ! -d "$DEMO_PATH" ]; then
	echo "No existe la demo: $DEMO_PATH" >&2
	exit 1
fi
DEMO_NAME="$(basename "$DEMO_PATH")"

# --- Resolucion del toolchain ----------------------------------------------
# Normaliza separadores de Windows (C:\\ruta) a posix (/c/ruta o C:/ruta) para
# que el script funcione igual en bash de Windows, Linux y macOS.
if [ -n "${AMIGA_BIN_PATH:-}" ]; then
	AMIGA_BIN_PATH="${AMIGA_BIN_PATH//\\//}"
fi

# Devuelve 0 y escribe la ruta en AMIGA_BIN cuando se encuentra.
find_toolchain() {
	if [ -n "${AMIGA_BIN_PATH:-}" ] && [ -d "$AMIGA_BIN_PATH" ]; then
		echo "$AMIGA_BIN_PATH"
		return 0
	fi
	# Fallback Windows (extensiones Cursor/VS Code).
	local cand
	for cand in \
		"$HOME/.cursor/extensions"/bartmanabyss.amiga-debug-*/bin/win32 \
		"$HOME/.vscode/extensions"/bartmanabyss.amiga-debug-*/bin/win32; do
		if [ -x "$cand/opt/bin/m68k-amiga-elf-g++.exe" ]; then
			echo "$cand"
			return 0
		fi
	done
	# Toolchain en PATH (Linux/macOS: m68k-amiga-elf-*).
	if command -v m68k-amiga-elf-gcc >/dev/null 2>&1; then
		echo ""
		return 0
	fi
	return 1
}

if ! find_toolchain >/dev/null 2>&1; then
	echo "No se encontro el toolchain. Define AMIGA_BIN_PATH o instala m68k-amiga-elf-* en el PATH." >&2
	exit 1
fi
TOOLCHAIN="$(find_toolchain)"

# Selecciona un binario del toolchain (por ruta o por nombre en PATH).
tool() {
	local name="$1"
	if [ -n "$TOOLCHAIN" ]; then
		# Las extensiones Windows distribuyen .exe; un toolchain Unix no.
		local candidate
		for candidate in \
			"$TOOLCHAIN/$name" \
			"$TOOLCHAIN/$name.exe" \
			"$TOOLCHAIN/opt/bin/$name" \
			"$TOOLCHAIN/opt/bin/$name.exe"; do
			if [ -x "$candidate" ]; then
				echo "$candidate"
				return 0
			fi
		done
		echo "$TOOLCHAIN/$name"
		return 0
	fi
	echo "$name"
	return 0
}

GCC="$(tool m68k-amiga-elf-gcc)"
GXX="$(tool m68k-amiga-elf-g++)"
ASM="$(tool m68k-amiga-elf-as)"
ELF2HUNK="$(tool elf2hunk)"
OBJDUMP="$(tool m68k-amiga-elf-objdump)"
SDKDIR=""
if [ -n "$TOOLCHAIN" ]; then
	SDKDIR="$TOOLCHAIN/opt/m68k-amiga-elf/sys-include"
else
	SDKDIR="$(dirname "$(command -v m68k-amiga-elf-gcc)")/../m68k-amiga-elf/sys-include"
fi

# --- Config ID (perfil de máquina + flags + modo) --------------------------
# El CONFIG_ID nombra el ejecutable y aísla los artefactos, de modo que
# distintas configuraciones (TARGET_MACHINE + EXTRA_DEFINES + debug/release)
# conviven sin pisarse. El token es canónico: MACHINE_fLAGS_modo.
#
#   TARGET_MACHINE=A500 (defecto) · A1200 · AtariST · Megadrive · NeoGeo
#   EXTRA_DEFINES="-DK_HUD=0 -DK_DUAL=1" → flags "k_hud_0_k_dual_1"
#   modo: debug | release
MACHINE_ID="${TARGET_MACHINE:-A500}"
GEN_FLAGS=""
if [ -n "${EXTRA_DEFINES:-}" ]; then
	# Sanitiza EXTRA_DEFINES a solo [a-z0-9_] y elimina el marcador de "-D"
	# (que el tr convierte a 'd'): "-DK_HUD=0 -DK_DUAL=1" -> "k_hud0_k_dual1".
	GEN_FLAGS="$(echo "$EXTRA_DEFINES" | tr -cd 'A-Za-z0-9_' | tr 'A-Z' 'a-z' | sed 's/^d//; s/_d/_/g')"
fi
GEN_MODE="release"
if [ "$DEBUG_BUILD" -eq 1 ]; then GEN_MODE="debug"; fi
if [ "$O0_BUILD" -eq 1 ]; then GEN_MODE="o0"; fi
CONFIG_ID="${MACHINE_ID}"
if [ -n "$GEN_FLAGS" ]; then CONFIG_ID="${CONFIG_ID}_${GEN_FLAGS}"; fi
CONFIG_ID="${CONFIG_ID}_${GEN_MODE}"

# --- Directorios de salida --------------------------------------------------
OBJ_DIR="$ROOT/obj/demos/$DEMO_NAME/$CONFIG_ID"
OUT_DIR="$ROOT/out/demos/$DEMO_NAME/$CONFIG_ID"

if [ "$CLEAN" -eq 1 ]; then
	rm -rf "$OBJ_DIR" "$OUT_DIR"
fi
mkdir -p "$OBJ_DIR" "$OUT_DIR"

# --- Flags ------------------------------------------------------------------
OPT="-Ofast"
if [ "$DEBUG_BUILD" -eq 1 ]; then
	# -O1 para la regresion automatica: suficiente para que las demos lleguen a
	# READY dentro del timeout en el 68000 emulado.
	OPT="-O1"
fi
if [ "$O0_BUILD" -eq 1 ]; then
	# -O0 para depuracion interactiva fiable: con -O1/-fomit-frame-pointer GDB
	# optimiza variables (context/synthetic pointer, saved_background
	# <optimized out>) y rompe el paso a paso fiel.
	OPT="-O0"
fi
COMMON=(
	"-g" "-MP" "-MMD" "-m68000" "$OPT" "-nostdlib" "-Wextra"
	"-Wno-unused-function" "-Wno-volatile-register-var"
	"-fomit-frame-pointer" "-fno-exceptions"
	"-ffunction-sections" "-fdata-sections"
	"-I$ROOT" "-I$ROOT/engine/include" "-I$SDKDIR"
)
# Macros extra reproducibles (p. ej. EXTRA_DEFINES="-DK_TILE_WIDTH=32 -DK_DUAL=0").
# Se aplican a C++ y C; suelen parametrizar la demo sin tocar el fuente.
EXTRA_DEFINES="${EXTRA_DEFINES:-}"
if [ -n "$EXTRA_DEFINES" ]; then
	COMMON+=($EXTRA_DEFINES)
fi
CPP_FLAGS=("${COMMON[@]}" "-std=gnu++23" "-fno-rtti" "-fno-threadsafe-statics" "-fno-use-cxa-atexit")
C_FLAGS=("${COMMON[@]}" "-std=gnu11" "-fno-tree-loop-distribution")

# --- Compilacion ------------------------------------------------------------
OBJECTS=()

# Objeto con ruta segura (reemplaza :\\/ por _) para conservar el arbol de
# fuentes del engine en el directorio obj.
object_path() {
	local source="$1"
	local rel="${source#"$ROOT"/}"
	echo "$OBJ_DIR/${rel//[:\/\\]/_}.o"
}

echo "[build-demo] $DEMO_NAME"
for src in $(find "$ROOT/engine/src" -name '*.cpp' | sort); do
	obj="$(object_path "$src")"
	OBJECTS+=("$obj")
	echo "  C++   $src"
	"$GXX" "${CPP_FLAGS[@]}" -c -o "$obj" "$src"
done
for src in $(find "$DEMO_PATH/src" -name '*.cpp' | sort); do
	obj="$(object_path "$src")"
	OBJECTS+=("$obj")
	echo "  C++   $src"
	"$GXX" "${CPP_FLAGS[@]}" -c -o "$obj" "$src"
done

SUPPORT_C="$ROOT/support/gcc8_c_support.c"
SUPPORT_C_OBJ="$OBJ_DIR/support_gcc8_c_support.o"
OBJECTS+=("$SUPPORT_C_OBJ")
echo "  C     $SUPPORT_C"
"$GCC" "${C_FLAGS[@]}" -c -o "$SUPPORT_C_OBJ" "$SUPPORT_C"

SUPPORT_ASM="$ROOT/support/gcc8_a_support.s"
SUPPORT_ASM_OBJ="$OBJ_DIR/support_gcc8_a_support.o"
OBJECTS+=("$SUPPORT_ASM_OBJ")
echo "  ASM   $SUPPORT_ASM"
"$ASM" -mcpu=68000 -g --register-prefix-optional "-I$SDKDIR" -o "$SUPPORT_ASM_OBJ" "$SUPPORT_ASM"

# --- Enlazado y hunk --------------------------------------------------------
ELF="$OUT_DIR/$DEMO_NAME.$CONFIG_ID.elf"
EXE="$OUT_DIR/$DEMO_NAME.$CONFIG_ID.exe"
MAP="$OUT_DIR/$DEMO_NAME.$CONFIG_ID.map"
LISTING="$OUT_DIR/$DEMO_NAME.$CONFIG_ID.s"

echo "  LINK  $ELF"
"$GXX" "${COMMON[@]}" "-Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$MAP" "${OBJECTS[@]}" -o "$ELF"

echo "  HUNK  $EXE"
"$ELF2HUNK" "$ELF" "$EXE"

"$OBJDUMP" --disassemble --no-show-raw-ins --visualize-jumps -S "$ELF" >"$LISTING"

echo "  OK    $EXE"
