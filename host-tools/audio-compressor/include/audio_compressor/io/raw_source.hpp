#pragma once

/// Fuente host de PCM8 firmado RAW con lectura acotada por ventanas.

#include <algorithm>
#include <fstream>
#include <string>

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::io {

class RawSource {
public:
	[[nodiscard]] bool open(const std::string& path, eng::u32 sample_rate) {
		m_file.open(path, std::ios::binary | std::ios::ate);
		if (!m_file || sample_rate == 0u) return false;
		const std::streamoff size = m_file.tellg();
		if (size <= 0) return false;
		m_frames = static_cast<eng::u64>(size);
		m_rate = sample_rate;
		m_file.clear();
		return true;
	}

	[[nodiscard]] eng::u32 sample_rate() const noexcept { return m_rate; }
	[[nodiscard]] eng::u64 frames() const noexcept { return m_frames; }

	[[nodiscard]] eng::usize read(eng::u64 offset, eng::Span<eng::u8> window) {
		if (!m_file || offset >= m_frames || window.empty()) return 0u;
		const eng::usize count = static_cast<eng::usize>(std::min<eng::u64>(window.size(), m_frames - offset));
		m_file.clear();
		m_file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
		return static_cast<eng::usize>(m_file.read(reinterpret_cast<char*>(window.data()),
			static_cast<std::streamsize>(count)).gcount());
	}

private:
	std::ifstream m_file;
	eng::u64 m_frames = 0u;
	eng::u32 m_rate = 0u;
};

} // namespace audio_compressor::io
