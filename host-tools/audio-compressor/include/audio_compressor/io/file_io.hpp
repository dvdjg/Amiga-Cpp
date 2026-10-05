#pragma once

/// E/S host estándar para conversión batch. El adaptador SDL3 vive en `sdl_file_io.hpp`.

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <eng/core/types/types.hpp>

namespace audio_compressor::io {

[[nodiscard]] inline bool read_file(const std::filesystem::path& path, std::vector<eng::u8>& bytes) {
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream) return false;
	const std::streamoff size = stream.tellg();
	if (size <= 0 || static_cast<std::uintmax_t>(size) > static_cast<std::uintmax_t>(static_cast<eng::usize>(-1))) return false;
	bytes.resize(static_cast<eng::usize>(size));
	stream.seekg(0, std::ios::beg);
	return static_cast<bool>(stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())));
}

[[nodiscard]] inline bool write_file(const std::filesystem::path& path, const std::vector<eng::u8>& bytes) {
	if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream) return false;
	stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	return static_cast<bool>(stream);
}

} // namespace audio_compressor::io
