#pragma once

/// Fuente host de WAV PCM que lee y mezcla una ventana sin cargar el fichero completo.

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::io {

class WavSource {
public:
	[[nodiscard]] bool open(const std::string& path) {
		m_file.open(path.c_str(), std::ios::in | std::ios::binary);
		if (!m_file) return false;
		std::array<eng::u8, 12> riff {};
		if (!read_bytes(riff.data(), riff.size()) || std::memcmp(riff.data(), "RIFF", 4u) != 0 ||
			std::memcmp(riff.data() + 8u, "WAVE", 4u) != 0) return false;
		bool have_format = false;
		bool have_data = false;
		while (m_file) {
			std::array<eng::u8, 8> chunk {};
			if (!read_bytes(chunk.data(), chunk.size())) break;
			const eng::u32 size = rd32(chunk.data() + 4u);
			const std::streamoff payload = m_file.tellg();
			if (std::memcmp(chunk.data(), "fmt ", 4u) == 0) {
				std::vector<eng::u8> format(size);
				if (size < 16u || !read_bytes(format.data(), format.size()) || rd16(format.data()) != 1u) return false;
				m_channels = rd16(format.data() + 2u);
				m_rate = rd32(format.data() + 4u);
				m_block_align = rd16(format.data() + 12u);
				m_bits = rd16(format.data() + 14u);
				have_format = m_channels != 0u && m_channels <= 8u && (m_bits == 8u || m_bits == 16u) &&
					m_rate != 0u && m_block_align == static_cast<eng::u16>(m_channels * (m_bits / 8u));
			} else if (std::memcmp(chunk.data(), "data", 4u) == 0) {
				m_data_offset = payload;
				m_data_bytes = size;
				m_file.seekg(static_cast<std::streamoff>(size), std::ios::cur);
				have_data = true;
			} else {
				m_file.seekg(static_cast<std::streamoff>(size + (size & 1u)), std::ios::cur);
			}
			if (have_format && have_data) break;
		}
		if (!have_format || !have_data || m_block_align == 0u || m_data_bytes % m_block_align != 0u) return false;
		m_frames = m_data_bytes / m_block_align;
		m_file.clear();
		return true;
	}

	[[nodiscard]] eng::u32 sample_rate() const noexcept { return m_rate; }
	[[nodiscard]] eng::u16 channels() const noexcept { return m_channels; }
	[[nodiscard]] eng::u64 frames() const noexcept { return m_frames; }

	/// Lee hasta `window.size()` frames desde `offset` y hace downmix saturado a PCM8 firmado.
	[[nodiscard]] eng::usize read(eng::u64 offset, eng::Span<eng::u8> window) {
		if (!m_file || offset >= m_frames || window.empty()) return 0u;
		const eng::usize count = static_cast<eng::usize>(std::min<eng::u64>(window.size(), m_frames - offset));
		std::vector<eng::u8> interleaved(count * m_block_align);
		m_file.clear();
		m_file.seekg(m_data_offset + static_cast<std::streamoff>(offset * m_block_align), std::ios::beg);
		if (!read_bytes(interleaved.data(), interleaved.size())) return 0u;
		for (eng::usize frame = 0u; frame < count; ++frame) {
			eng::s32 sum = 0;
			for (eng::u16 channel = 0u; channel < m_channels; ++channel) {
				const eng::usize at = frame * m_block_align + channel * (m_bits / 8u);
				const eng::s32 sample = m_bits == 8u ? static_cast<eng::s32>(interleaved[at]) - 128 :
					static_cast<eng::s32>(static_cast<eng::s16>(static_cast<eng::u16>(interleaved[at]) |
						(static_cast<eng::u16>(interleaved[at + 1u]) << 8u))) >> 8;
				sum += sample;
			}
			const eng::s32 mixed = sum / static_cast<eng::s32>(m_channels);
			window[frame] = static_cast<eng::u8>(std::clamp(mixed, -128, 127));
		}
		return count;
	}

private:
	[[nodiscard]] bool read_bytes(void* destination, std::size_t size) {
		return static_cast<bool>(m_file.read(static_cast<char*>(destination), static_cast<std::streamsize>(size)));
	}
	[[nodiscard]] static eng::u16 rd16(const eng::u8* bytes) noexcept {
		return static_cast<eng::u16>(bytes[0]) | static_cast<eng::u16>(static_cast<eng::u16>(bytes[1]) << 8u);
	}
	[[nodiscard]] static eng::u32 rd32(const eng::u8* bytes) noexcept {
		return static_cast<eng::u32>(bytes[0]) | (static_cast<eng::u32>(bytes[1]) << 8u) |
			(static_cast<eng::u32>(bytes[2]) << 16u) | (static_cast<eng::u32>(bytes[3]) << 24u);
	}

	std::ifstream m_file;
	std::streamoff m_data_offset = 0;
	eng::u32 m_data_bytes = 0u;
	eng::u32 m_rate = 0u;
	eng::u64 m_frames = 0u;
	eng::u16 m_channels = 0u;
	eng::u16 m_block_align = 0u;
	eng::u16 m_bits = 0u;
};

} // namespace audio_compressor::io
