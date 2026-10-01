#pragma once

/// E/S host portable para el compresor. SDL3 es la implementación preferida cuando está habilitada;
/// el fallback de la conversión batch usa streams estándar para que los tests no necesiten SDL3.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <eng/core/types/types.hpp>

#if defined(AUDIO_COMPRESSOR_SDL3)
#include <SDL3/SDL_iostream.h>
#endif

namespace audio_compressor::io {

[[nodiscard]] inline bool read_file(const std::filesystem::path& path, std::vector<eng::u8>& bytes) {
#if defined(AUDIO_COMPRESSOR_SDL3)
	SDL_IOStream* stream = SDL_IOFromFile(path.string().c_str(), "rb");
	if (stream == nullptr) return false;
	const Sint64 size = SDL_GetIOSize(stream);
	if (size <= 0 || static_cast<Uint64>(size) > static_cast<Uint64>(static_cast<eng::usize>(-1))) {
		SDL_CloseIO(stream);
		return false;
	}
	bytes.resize(static_cast<eng::usize>(size));
	const size_t read = SDL_ReadIO(stream, bytes.data(), bytes.size());
	SDL_CloseIO(stream);
	return read == bytes.size();
#else
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream) return false;
	const std::streamoff size = stream.tellg();
	if (size <= 0 || static_cast<std::uintmax_t>(size) > static_cast<std::uintmax_t>(static_cast<eng::usize>(-1))) return false;
	bytes.resize(static_cast<eng::usize>(size));
	stream.seekg(0, std::ios::beg);
	return static_cast<bool>(stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())));
#endif
}

[[nodiscard]] inline bool write_file(const std::filesystem::path& path, const std::vector<eng::u8>& bytes) {
	if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
#if defined(AUDIO_COMPRESSOR_SDL3)
	SDL_IOStream* stream = SDL_IOFromFile(path.string().c_str(), "wb");
	if (stream == nullptr) return false;
	const size_t written = SDL_WriteIO(stream, bytes.data(), bytes.size());
	const bool closed = SDL_CloseIO(stream) == 0;
	return written == bytes.size() && closed;
#else
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream) return false;
	stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(stream);
#endif
}

} // namespace audio_compressor::io
