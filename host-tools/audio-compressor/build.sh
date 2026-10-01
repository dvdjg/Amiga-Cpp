#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/out/tmp/audio-compressor"
CXX="${CXX:-g++}"
TOOLCHAIN_BIN="$(dirname "$(command -v "$CXX")")"
SDL3=0
for arg in "$@"; do
	case "$arg" in
		--sdl3) SDL3=1 ;;
		--help) printf '%s\n' 'Uso: host-tools/audio-compressor/build.sh [--sdl3]' ; exit 0 ;;
		*) printf 'Opción desconocida: %s\n' "$arg" >&2; exit 2 ;;
	esac
done
mkdir -p "$OUT"
FLAGS=(-B"$TOOLCHAIN_BIN" -std=gnu++23 -O2 -Wall -Wextra -Werror=narrowing -I"$ROOT/engine/include" -I"$ROOT/host-tools/pack-pcm" -I"$ROOT/host-tools/audio-compressor/include" -static-libgcc -static-libstdc++)
LIBS=()
if [ "$SDL3" -eq 1 ]; then
	FLAGS+=(-DAUDIO_COMPRESSOR_SDL3=1)
	# El ejecutable no debe depender de SDL3.dll ni de los runtimes MinGW. Las únicas DLL esperadas
	# después son APIs/servicios del sistema Windows y el driver de audio elegido por SDL.
	# SDL añade sus librerías de sistema; el runtime C++/GCC ya está estático en la configuración común.
	if command -v pkg-config >/dev/null 2>&1 && pkg-config --exists sdl3; then
		read -r -a SDL_CFLAGS <<<"$(pkg-config --cflags sdl3)"
		read -r -a SDL_LIBS <<<"$(pkg-config --static --libs sdl3)"
		FLAGS+=("${SDL_CFLAGS[@]}")
		LIBS+=("${SDL_LIBS[@]}")
	else
		SDL_ROOT="${SDL3_ROOT:-${SDL3_DIR:-}}"
		if [ -z "$SDL_ROOT" ]; then printf '%s\n' 'SDL3 no encontrado: define SDL3_ROOT/SDL3_DIR o instala pkg-config.' >&2; exit 1; fi
		SDL_ROOT_POSIX="$(cygpath -u "$SDL_ROOT" 2>/dev/null || printf '%s' "$SDL_ROOT")"
		if [ -f "$SDL_ROOT_POSIX/lib/pkgconfig/sdl3.pc" ] && command -v pkg-config >/dev/null 2>&1; then
			read -r -a SDL_CFLAGS <<<"$(PKG_CONFIG_PATH="$SDL_ROOT_POSIX/lib/pkgconfig" pkg-config --cflags sdl3)"
			read -r -a SDL_LIBS <<<"$(PKG_CONFIG_PATH="$SDL_ROOT_POSIX/lib/pkgconfig" pkg-config --static --libs sdl3)"
			FLAGS+=("${SDL_CFLAGS[@]}"); LIBS+=("${SDL_LIBS[@]}")
		elif [ -f "$SDL_ROOT_POSIX/lib/libSDL3.a" ]; then LIBS+=("$SDL_ROOT_POSIX/lib/libSDL3.a")
		elif [ -f "$SDL_ROOT_POSIX/lib/libSDL3-static.a" ]; then LIBS+=("$SDL_ROOT_POSIX/lib/libSDL3-static.a")
		else printf '%s\n' "SDL3 estático no encontrado bajo $SDL_ROOT/lib." >&2; exit 1; fi
	fi
fi
if [ "$SDL3" -eq 1 ]; then
	# Debe aparecer después de SDL3: el linker de MinGW procesa archivos estáticos en una pasada.
	WINPTHREAD_STATIC="$("$CXX" -print-file-name=libwinpthread.a 2>/dev/null || true)"
	if [ -f "$WINPTHREAD_STATIC" ]; then
		LIBS+=(-Wl,-Bstatic "$WINPTHREAD_STATIC" -Wl,-Bdynamic)
	else
		LIBS+=(-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
	fi
fi
"$CXX" "${FLAGS[@]}" "$ROOT/host-tools/audio-compressor/src/main.cpp" "${LIBS[@]}" -o "$OUT/audio-compressor"
