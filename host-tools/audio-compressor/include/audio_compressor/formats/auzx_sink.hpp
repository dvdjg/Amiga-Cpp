#pragma once

/// Sink AUZX incremental: reserva cabecera/índice, añade payloads secuencialmente y parchea el
/// índice al finalizar. La memoria usada por payloads es la ventana del llamador, no el audio.

#include <filesystem>
#include <fstream>
#include <vector>

#include <eng/audio/auzx.hpp>
#include <eng/core/types/types.hpp>

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
		header[0] = 'A'; header[1] = 'U'; header[2] = 'Z'; header[3] = 'X'; header[4] = 1u; header[5] = m_codec; header[10] = 8u;
		wr16(header, 6u, m_rate); wr16(header, 8u, 1u); wr32(header, 12u, m_samples); wr16(header, 16u, m_chunk_samples);
		wr16(header, 18u, static_cast<eng::u16>(m_offsets.size())); wr32(header, 20u, eng::audio::auzx::kHeaderSize);
		wr32(header, 24u, m_offsets.front());
		m_file.seekp(0, std::ios::beg); m_file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
		for (eng::usize i = 0u; i < m_offsets.size(); ++i) {
			std::vector<eng::u8> entry(eng::audio::auzx::kChunkEntrySize, 0u);
			wr32(entry, 0u, m_offsets[i]); wr32(entry, 4u, m_sizes[i]);
			m_file.write(reinterpret_cast<const char*>(entry.data()), static_cast<std::streamsize>(entry.size()));
		}
		m_file.flush(); m_file.close();
		return static_cast<bool>(m_file) || !m_file.fail();
	}

private:
	static void wr16(std::vector<eng::u8>& bytes, eng::usize at, eng::u16 value) {
		bytes[at] = static_cast<eng::u8>(value); bytes[at + 1u] = static_cast<eng::u8>(value >> 8u);
	}
	static void wr32(std::vector<eng::u8>& bytes, eng::usize at, eng::u32 value) {
		bytes[at] = static_cast<eng::u8>(value); bytes[at + 1u] = static_cast<eng::u8>(value >> 8u);
		bytes[at + 2u] = static_cast<eng::u8>(value >> 16u); bytes[at + 3u] = static_cast<eng::u8>(value >> 24u);
	}
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
