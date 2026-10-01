#pragma once

/// Sink AUZX incremental: reserva cabecera/índice, añade payloads secuencialmente y parchea el
/// índice al finalizar. La memoria usada por payloads es la ventana del llamador, no el audio.

#include <filesystem>
#include <fstream>
#include <vector>

#include <eng/audio/auzx.hpp>
#include <eng/core/types/types.hpp>
#include "binary.hpp"

namespace audio_compressor::formats {

class AuzxSink {
public:
	[[nodiscard]] bool open(const std::filesystem::path& path, eng::u16 rate, eng::u32 samples,
		eng::u16 chunk_samples, eng::u16 chunks, eng::u8 codec) {
		if (rate == 0u || samples == 0u || chunk_samples == 0u || chunks == 0u) return false;
		if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
		m_file.open(path, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
		if (!m_file) return false;
		m_offsets.assign(chunks, 0u); m_sizes.assign(chunks, 0u); m_next = 0u;
		std::vector<eng::u8> zero(eng::audio::auzx::kHeaderSize + chunks * eng::audio::auzx::kChunkEntrySize, 0u);
		m_file.write(reinterpret_cast<const char*>(zero.data()), static_cast<std::streamsize>(zero.size()));
		m_cursor = zero.size();
		m_rate = rate; m_samples = samples; m_chunk_samples = chunk_samples; m_codec = codec;
		return static_cast<bool>(m_file);
	}

	[[nodiscard]] bool append(eng::Span<const eng::u8> payload) {
		if (!m_file || m_next >= m_offsets.size() || payload.empty() || m_cursor > 0xffffffffu - payload.size()) return false;
		m_offsets[m_next] = static_cast<eng::u32>(m_cursor);
		m_sizes[m_next] = static_cast<eng::u32>(payload.size());
		m_file.seekp(static_cast<std::streamoff>(m_cursor), std::ios::beg);
		m_file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
		if (!m_file) return false;
		m_cursor += payload.size(); ++m_next;
		return true;
	}

	[[nodiscard]] bool finalize() {
		if (!m_file || m_next != m_offsets.size() || m_cursor > 0xffffffffu) return false;
		std::vector<eng::u8> header(eng::audio::auzx::kHeaderSize, 0u);
		formats::BinaryWriter header_writer {header};
		header[0] = 'A'; header[1] = 'U'; header[2] = 'Z'; header[3] = 'X'; header[4] = 1u;
		(void)header_writer.u8(5u, m_codec); (void)header_writer.u16(6u, m_rate); (void)header_writer.u16(8u, 1u);
		(void)header_writer.u8(10u, 8u); (void)header_writer.u32(12u, m_samples); (void)header_writer.u16(16u, m_chunk_samples);
		(void)header_writer.u16(18u, static_cast<eng::u16>(m_offsets.size()));
		(void)header_writer.u32(20u, eng::audio::auzx::kHeaderSize); (void)header_writer.u32(24u, m_offsets.front());
		m_file.seekp(0, std::ios::beg); m_file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
		for (eng::usize i = 0u; i < m_offsets.size(); ++i) {
			std::vector<eng::u8> entry(eng::audio::auzx::kChunkEntrySize, 0u);
			formats::BinaryWriter entry_writer {entry};
			(void)entry_writer.u32(0u, m_offsets[i]); (void)entry_writer.u32(4u, m_sizes[i]);
			m_file.write(reinterpret_cast<const char*>(entry.data()), static_cast<std::streamsize>(entry.size()));
		}
		m_file.flush(); m_file.close();
		return static_cast<bool>(m_file) || !m_file.fail();
	}

	private:
	std::fstream m_file;
	std::vector<eng::u32> m_offsets;
	std::vector<eng::u32> m_sizes;
	eng::usize m_cursor = 0u;
	eng::u32 m_rate = 0u;
	eng::u32 m_samples = 0u;
	eng::u16 m_chunk_samples = 0u;
	eng::u8 m_codec = 0u;
	eng::usize m_next = 0u;
};

} // namespace audio_compressor::formats
