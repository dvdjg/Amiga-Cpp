#pragma once

/// Fuente WAV intercalada: conserva metadatos y lee un canal por ventanas sin cargar todos los stems.

#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

#include "../domain/audio_types.hpp"

namespace audio_compressor::io {

class WavStemSource {
public:
	/// Abre un WAV PCM y localiza el chunk `data` sin copiarlo a un buffer completo.
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
			const eng::u32 size = read_u32(chunk.data() + 4u);
			const std::streamoff payload = m_file.tellg();
			if (std::memcmp(chunk.data(), "fmt ", 4u) == 0) {
				std::array<eng::u8, 16> format {};
				if (size < format.size() || !read_bytes(format.data(), format.size()) || read_u16(format.data()) != 1u) return false;
				m_channels = read_u16(format.data() + 2u); m_rate = read_u32(format.data() + 4u);
				m_block_align = read_u16(format.data() + 12u); m_bits = read_u16(format.data() + 14u);
				if (size > format.size()) m_file.seekg(static_cast<std::streamoff>(size - format.size()), std::ios::cur);
				have_format = m_channels != 0u && m_channels <= 8u && (m_bits == 8u || m_bits == 16u) &&
					m_rate != 0u && m_rate <= 65535u && m_block_align == static_cast<eng::u16>(m_channels * (m_bits / 8u));
			} else if (std::memcmp(chunk.data(), "data", 4u) == 0) {
				m_data_offset = payload; m_data_bytes = size;
				m_file.seekg(static_cast<std::streamoff>(size), std::ios::cur); have_data = true;
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

	/// Devuelve el número de canales independientes.
	[[nodiscard]] eng::u16 channels() const noexcept { return m_channels; }
	/// Devuelve la duración de cada canal en frames.
	[[nodiscard]] eng::u64 frames() const noexcept { return m_frames; }
	/// Devuelve el formato original de la fuente intercalada.
	[[nodiscard]] domain::AudioFormat format() const noexcept { return {m_rate, m_channels, m_bits, m_bits == 8u}; }

	/// Lee un canal concreto y lo normaliza a PCM8 firmado en el buffer del llamador.
	[[nodiscard]] eng::usize read_channel(eng::u16 channel, eng::u64 offset, eng::Span<eng::u8> output) {
		if (!m_file || channel >= m_channels || offset >= m_frames || output.empty()) return 0u;
		const eng::usize count = static_cast<eng::usize>(std::min<eng::u64>(output.size(), m_frames - offset));
		m_file.clear(); m_file.seekg(m_data_offset + static_cast<std::streamoff>(offset * m_block_align + channel * (m_bits / 8u)), std::ios::beg);
		for (eng::usize i = 0u; i < count; ++i) {
			std::array<eng::u8, 2> sample {};
			if (!read_bytes(sample.data(), m_bits / 8u)) return i;
			const eng::s32 value = m_bits == 8u ? static_cast<eng::s32>(sample[0]) - 128 :
				static_cast<eng::s32>(static_cast<eng::s16>(read_u16(sample.data())) >> 8u);
			output[i] = static_cast<eng::u8>(std::clamp(value, -128, 127));
			if (m_block_align > m_bits / 8u) m_file.seekg(static_cast<std::streamoff>(m_block_align - m_bits / 8u), std::ios::cur);
		}
		return count;
	}

private:
	[[nodiscard]] bool read_bytes(void* destination, std::size_t size) { return static_cast<bool>(m_file.read(static_cast<char*>(destination), static_cast<std::streamsize>(size))); }
	[[nodiscard]] static eng::u16 read_u16(const eng::u8* bytes) noexcept { return static_cast<eng::u16>(bytes[0]) | static_cast<eng::u16>(static_cast<eng::u16>(bytes[1]) << 8u); }
	[[nodiscard]] static eng::u32 read_u32(const eng::u8* bytes) noexcept { return static_cast<eng::u32>(bytes[0]) | (static_cast<eng::u32>(bytes[1]) << 8u) | (static_cast<eng::u32>(bytes[2]) << 16u) | (static_cast<eng::u32>(bytes[3]) << 24u); }
	std::ifstream m_file; ///< Stream intercalado abierto para lecturas posicionadas.
	std::streamoff m_data_offset = 0; ///< Inicio del chunk data en bytes.
	eng::u32 m_data_bytes = 0u; ///< Tamaño del chunk data.
	eng::u32 m_rate = 0u; ///< Frecuencia PCM original.
	eng::u64 m_frames = 0u; ///< Frames por canal.
	eng::u16 m_channels = 0u; ///< Canales intercalados.
	eng::u16 m_block_align = 0u; ///< Bytes por frame intercalado.
	eng::u16 m_bits = 0u; ///< Profundidad PCM original.
};

} // namespace audio_compressor::io
