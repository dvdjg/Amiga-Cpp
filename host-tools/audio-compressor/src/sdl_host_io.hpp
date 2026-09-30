#pragma once

/// \file sdl_host_io.hpp
/// E/S host portable de la utilidad usando SDL3 cuando está habilitado.
///
/// La aplicación no conoce Win32 para leer ficheros: SDL3 proporciona el camino portable y el
/// fallback C permite compilar el conversor sin SDL3. La integración con `ffmpeg` sigue siendo un
/// proceso externo porque no forma parte de la API de E/S de SDL.

#include <cstdio>
#include <vector>

#include <eng/core/types/types.hpp>

#if defined(AUDIO_COMPRESSOR_SDL3)
#include <SDL3/SDL_iostream.h>
#endif

namespace audio_compressor {

/// Lee un fichero completo usando SDL3 o el fallback host sin SDL3.
[[nodiscard]] inline bool load_file(const char* path, std::vector<eng::u8>& bytes) {
#if defined(AUDIO_COMPRESSOR_SDL3)
	size_t size = 0u;
	void* data = SDL_LoadFile(path, &size);
	if (!data || size <= 0) { SDL_free(data); return false; }
	bytes.assign(static_cast<const eng::u8*>(data), static_cast<const eng::u8*>(data) + size);
	SDL_free(data);
	return true;
#else
	std::FILE* file = std::fopen(path, "rb");
	if (!file) return false;
	std::fseek(file, 0, SEEK_END); const long size = std::ftell(file); std::fseek(file, 0, SEEK_SET);
	if (size <= 0) { std::fclose(file); return false; }
	bytes.resize(static_cast<eng::usize>(size));
	const bool ok = std::fread(bytes.data(), 1u, bytes.size(), file) == bytes.size();
	std::fclose(file);
	return ok;
#endif
}

} // namespace audio_compressor
