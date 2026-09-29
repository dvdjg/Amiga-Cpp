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

# --- Id de build (por ruta para features) -----------------------------------
# El id de build/out aísla los artefactos. `techniques/` usa el leaf (nombres únicos
# por construcción); `features/<feature>/<plataforma>/NNN_<tema>` usa la **ruta** relativa
# a `demos/features/` (mismo nombre de demo en varias plataformas → no se machacan).
# La variante de build (A500/A1200/ST/STE) ya va en el CONFIG_ID.
DEMO_REL="${DEMO//\\//}"
case "$DEMO_REL" in
	demos/features/*) DEMO_ID="$(printf '%s' "${DEMO_REL#demos/features/}" | tr '/' '_')" ;;
	*) DEMO_ID="$DEMO_NAME" ;;
esac

# --- Resolucion del toolchain ----------------------------------------------
# Normaliza separadores de Windows (C:\\ruta) a posix (/c/ruta o C:/ruta) para
# que el script funcione igual en bash de Windows, Linux y macOS.
if [ -n "${AMIGA_BIN_PATH:-}" ]; then
	AMIGA_BIN_PATH="${AMIGA_BIN_PATH//\\//}"
fi

# Version de gcc de un candidato (vacio si esa ruta no tiene toolchain). Se usa `--version`
# porque el g++ cross de Bartman no responde a `-dumpversion`.
toolchain_version() {
	local cand="$1" gxx
	for gxx in \
		"$cand/opt/bin/m68k-amiga-elf-g++.exe" "$cand/m68k-amiga-elf-g++.exe" \
		"$cand/opt/bin/m68k-amiga-elf-g++" "$cand/m68k-amiga-elf-g++"; do
		if [ -x "$gxx" ]; then
			# Primera linea, p. ej. "m68k-amiga-elf-g++.exe (GCC) 15.1.0" -> 15.1.0.
			"$gxx" --version 2>/dev/null |
				awk 'NR==1{for(i=1;i<=NF;i++) if($i ~ /^[0-9]+\.[0-9]/) v=$i} END{print v}'
			return 0
		fi
	done
	echo ""
	return 0
}

# Elige el toolchain de **version mas alta** entre AMIGA_BIN_PATH y las extensiones de
# Cursor/VS Code (asi una extension nueva con gcc mas moderno gana sin tocar el entorno).
# Devuelve 0 y escribe la ruta (posiblemente vacia = usar PATH) en stdout.
find_toolchain() {
	local best="" best_ver="" cand ver
	for cand in \
		"${AMIGA_BIN_PATH:-}" \
		$HOME/.cursor/extensions/bartmanabyss.amiga-debug-*/bin/win32 \
		$HOME/.vscode/extensions/bartmanabyss.amiga-debug-*/bin/win32; do
		[ -n "$cand" ] && [ -d "$cand" ] || continue
		ver="$(toolchain_version "$cand")"
		[ -n "$ver" ] || continue
		if [ -z "$best_ver" ]; then
			best="$cand"
			best_ver="$ver"
			continue
		fi
		if [ "$ver" != "$best_ver" ] &&
		   [ "$(printf '%s\n%s\n' "$best_ver" "$ver" | sort -V | tail -1)" = "$ver" ]; then
			best="$cand"
			best_ver="$ver"
		fi
	done
	if [ -n "$best" ]; then
		echo "$best"
		return 0
	fi
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
if [ -n "$TOOLCHAIN" ]; then
	echo "[build] toolchain: $TOOLCHAIN (gcc $(toolchain_version "$TOOLCHAIN"))" >&2
else
	echo "[build] toolchain: m68k-amiga-elf-* del PATH" >&2
fi

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
VASM="$(tool vasmm68k_mot)"
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
OBJ_DIR="$ROOT/obj/demos/$DEMO_ID/$CONFIG_ID"
OUT_DIR="$ROOT/out/demos/$DEMO_ID/$CONFIG_ID"

if [ "$CLEAN" -eq 1 ]; then
	rm -rf "$OBJ_DIR" "$OUT_DIR"
fi
mkdir -p "$OBJ_DIR" "$OUT_DIR"

# --- Hook de assets (prebuild) ---------------------------------------------
# Si la demo trae `src/prebuild.sh`, se ejecuta ANTES de compilar para regenerar
# sus assets (p. ej. un blob UAF-R que luego se incbina). Se lanza desde la raiz
# del repo para que las rutas `out/assets/...` sean las canonicas. Es el gancho
# que hace reproducible el flujo exportador -> incbin -> runtime (como la 078).
if [ -f "$DEMO_PATH/src/prebuild.sh" ]; then
	echo "[build-demo] prebuild $DEMO_NAME"
	( cd "$ROOT" && MACHINE_ID="$MACHINE_ID" TARGET_MACHINE="$MACHINE_ID" bash "$DEMO_PATH/src/prebuild.sh" )
fi

# --- Flags ------------------------------------------------------------------
# Nivel de optimizacion de release. Medido en la 086 (A500_release, BUILD medido
# 2026-09-17) sobre la misma fuente, contador de ciclos del Amiga:
#
#   -O0  25,1 campos   (sin optimizar)
#   -O1   7,4 campos
#   -O2   7,0 campos   <- mejor
#   -Os  15,9 campos   <- 2,3x PEOR que -O2
#
# `-Os` NO es "codigo compacto = mas rapido" en este engine: al no respetar
# `always_inline` de la cadena caliente, gcc deshace la abstraccion de
# `ListBuilder`/`Scheduler` (llama a `ListBuilder::move` como funcion y mete
# `memset`/`memcpy`, con marco de 568 B en `build_frame`) y cuesta un `jsr` por
# MOVE de copper. Ver docs/guides/optimization/OPTIMIZACION_GPP_68000.md.
# La biseccion por unidad dio: engine a `-Os` no pierde nada; el coste lo mete la
# DEMO compilada a `-Os` (11,2 campos) y se agrava al combinarla con el resto.
OPT="-O2"
if [ "$DEBUG_BUILD" -eq 1 ]; then
	# -O1 para la regresion automatica: suficiente para que las demos lleguen a
	# READY dentro del timeout en el 68000 emulado. Es el perfil verde conocido.
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
	"-DENG_AMIGA=1"
	"-I$ROOT" "-I$ROOT/engine/include" "-I$SDKDIR"
)
# Macros extra reproducibles (p. ej. EXTRA_DEFINES="-DK_TILE_WIDTH=32 -DK_DUAL=0").
# Se aplican a C++ y C; suelen parametrizar la demo sin tocar el fuente.
EXTRA_DEFINES="${EXTRA_DEFINES:-}"
if [ -n "$EXTRA_DEFINES" ]; then
	COMMON+=($EXTRA_DEFINES)
fi

# Overrides por demo (`build.args` en el dir de la demo): una asignacion `CLAVE=valor` por
# linea (comentarios con `#`). Claves admitidas: ENGINE_OPT / DEMO_OPT / C_OPT. Mismo espiritu
# que `run.args` (opciones de ejecucion), pero para el build. No cambia el CONFIG_ID. Caso de
# uso: la 212 fija `DEMO_OPT=-O2` por el bug de codegen de gcc 15 m68k a `-O1`.
if [ -f "$DEMO_PATH/build.args" ]; then
	while IFS='=' read -r _k _v || [ -n "$_k" ]; do
		_k="${_k%%[[:space:]]*}"
		case "$_k" in
			""|\#*) continue ;;
			ENGINE_OPT) ENGINE_OPT="$_v" ;;
			DEMO_OPT) DEMO_OPT="$_v" ;;
			C_OPT) C_OPT="$_v" ;;
			FAST_STACK) FAST_STACK="$_v" ;;
		esac
	done <"$DEMO_PATH/build.args"
fi

# **Pila en Fast RAM** (opcional, por app): `FAST_STACK=1` en el `build.args` de la demo mueve la
# pila del hilo principal (y el SSP/IRQs en modo supervisor) a Fast RAM si existe (`_start` en
# `support/gcc8_c_support.c`, ver `INTERNAL_TYPE_SYSTEM.md` §3.8). Se expone como parámetro de
# compilacion para que **cada app elija**.
if [ "${FAST_STACK:-0}" = "1" ]; then
	COMMON+=("-DENG_FAST_STACK=1")
fi

# --- Flags por origen (override para bisecar un cuelgue de optimizacion) -----
# ENGINE_OPT / DEMO_OPT / C_OPT permiten compilar el engine, la demo y el
# soporte C con niveles distintos de $OPT sin cambiar el CONFIG_ID. Default:
# heredan $OPT (comportamiento normal). Útil para aislar qué unidad crashea
# a optimización alta (el caso release de la demo 107).
ENGINE_OPT="${ENGINE_OPT:-$OPT}"
DEMO_OPT="${DEMO_OPT:-$OPT}"
C_OPT="${C_OPT:-$OPT}"
cpp_flags_for() {
	local opt="$1" f
	local out=()
	for f in "${COMMON[@]}"; do
		if [ "$f" = "$OPT" ]; then out+=("$opt"); else out+=("$f"); fi
	done
	out+=("-std=gnu++23" "-fno-rtti" "-fno-threadsafe-statics" "-fno-use-cxa-atexit")
	printf '%s\n' "${out[@]}"
}
ENGINE_CPP_FLAGS="$(cpp_flags_for "$ENGINE_OPT")"
DEMO_CPP_FLAGS="$(cpp_flags_for "$DEMO_OPT")"
c_flags_for() {
	local opt="$1" f
	local out=()
	for f in "${COMMON[@]}"; do
		if [ "$f" = "$OPT" ]; then out+=("$opt"); else out+=("$f"); fi
	done
	out+=("-std=gnu11" "-fno-tree-loop-distribution")
	printf '%s\n' "${out[@]}"
}
C_FLAGS="$(c_flags_for "$C_OPT")"

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
	"$GXX" ${ENGINE_CPP_FLAGS} -c -o "$obj" "$src"
done
for src in $(find "$DEMO_PATH/src" -name '*.cpp' | sort); do
	obj="$(object_path "$src")"
	OBJECTS+=("$obj")
	echo "  C++   $src"
	"$GXX" ${DEMO_CPP_FLAGS} -c -o "$obj" "$src"
done

SUPPORT_C="$ROOT/support/gcc8_c_support.c"
SUPPORT_C_OBJ="$OBJ_DIR/support_gcc8_c_support.o"
OBJECTS+=("$SUPPORT_C_OBJ")
echo "  C     $SUPPORT_C"
"$GCC" ${C_FLAGS} -c -o "$SUPPORT_C_OBJ" "$SUPPORT_C"

# Ensambla TODOS los .s de support/ (gcc8_a_support.s + asm de demoscene como
# c2p_1x1_4.s). Un .s por objeto, con el nombre derivado para evitar colisiones.
for SUPPORT_ASM in $(find "$ROOT/support" -maxdepth 1 -name '*.s' | sort); do
	SUPPORT_ASM_NAME="$(basename "$SUPPORT_ASM" .s)"
	SUPPORT_ASM_OBJ="$OBJ_DIR/support_${SUPPORT_ASM_NAME}.o"
	OBJECTS+=("$SUPPORT_ASM_OBJ")
	echo "  ASM   $SUPPORT_ASM"
	"$ASM" -mcpu=68000 -g --register-prefix-optional "-I$SDKDIR" -o "$SUPPORT_ASM_OBJ" "$SUPPORT_ASM"
done

# Ensambla los fuentes VASM a ELF (compatibles con el linker de GNU):
#   - support/audio_mixer/mixer.asm  : Audio Mixer 3.7 (Photon).
#   - support/music/*.asm            : reproductores de música (p61, pt, ahx).
# Cada fuente usa sus includes propios (mixer_config.i, P6112-Play.i, ...).
for VASM_SRC in $(find "$ROOT/support/audio_mixer" -maxdepth 1 -name 'mixer.asm'; find "$ROOT/support/music" -maxdepth 1 -name '*.asm' | sort); do
	VASM_SRC_NAME="$(basename "$VASM_SRC" .asm)"
	VASM_SRC_DIR="$(basename "$(dirname "$VASM_SRC")")"
	VASM_OBJ="$OBJ_DIR/vasm_${VASM_SRC_DIR}_${VASM_SRC_NAME}.o"
	OBJECTS+=("$VASM_OBJ")
	echo "  VASM  $VASM_SRC"
	"$VASM" -quiet -Felf -m68000 -allmp -I"$SDKDIR" -I"$(dirname "$VASM_SRC")" -DBUILD_MIXER -o "$VASM_OBJ" "$VASM_SRC"
done

# --- Enlazado y hunk --------------------------------------------------------
ELF="$OUT_DIR/$DEMO_ID.$CONFIG_ID.elf"
EXE="$OUT_DIR/$DEMO_ID.$CONFIG_ID.exe"
MAP="$OUT_DIR/$DEMO_ID.$CONFIG_ID.map"
LISTING="$OUT_DIR/$DEMO_ID.$CONFIG_ID.s"

echo "  LINK  $ELF"
"$GXX" "${COMMON[@]}" "-Wl,--emit-relocs,--gc-sections,-Ttext=0x400,-Map=$MAP" "${OBJECTS[@]}" -o "$ELF"

echo "  HUNK  $EXE"
"$ELF2HUNK" "$ELF" "$EXE"

"$OBJDUMP" --disassemble --no-show-raw-ins --visualize-jumps -S "$ELF" >"$LISTING"

echo "  OK    $EXE"
