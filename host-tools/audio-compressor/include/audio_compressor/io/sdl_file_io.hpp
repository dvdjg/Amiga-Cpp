#pragma once

/// Adaptador opcional SDL3 para E/S host; no forma parte de la ruta batch por defecto.

#include <SDL3/SDL_iostream.h>
#include <filesystem>
#include <vector>

#include <eng/core/types/types.hpp>

namespace audio_compressor::io::sdl {

/// Lee un archivo completo mediante SDL_IOStream para herramientas que ya usan SDL3.
[[nodiscard]] inline bool read_file(const std::filesystem::path& path, std::vector<eng::u8>& bytes) {
	SDL_IOStream* stream = SDL_IOFromFile(path.string().c_str(), "rb");
	if (stream == nullptr) return false;
	const Sint64 size = SDL_GetIOSize(stream);
	if (size <= 0) { SDL_CloseIO(stream); return false; }
	bytes.resize(static_cast<eng::usize>(size));
	const size_t read = SDL_ReadIO(stream, bytes.data(), bytes.size());
	SDL_CloseIO(stream);
	return read == bytes.size();
}

/// Escribe un archivo completo mediante SDL_IOStream.
[[nodiscard]] inline bool write_file(const std::filesystem::path& path, const std::vector<eng::u8>& bytes) {
	if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
	SDL_IOStream* stream = SDL_IOFromFile(path.string().c_str(), "wb");
	if (stream == nullptr) return false;
	const size_t written = SDL_WriteIO(stream, bytes.data(), bytes.size());
	const bool closed = SDL_CloseIO(stream) == 0;
	return written == bytes.size() && closed;
}

} // namespace audio_compressor::io::sdl
