#pragma once

/// \file acp1_writer.hpp
/// Serializador host-only de stems AUZX al subconjunto estructural ACP1 v1.

#include <vector>

#include <eng/audio/acp1.hpp>

namespace audio_compressor {

/// Serializa payloads AUZX completos como unidades y eventos sincronizados; `false` indica datos inválidos.
[[nodiscard]] inline bool build_acp1(const std::vector<std::vector<eng::u8>>& payloads,
	eng::u32 sample_rate, eng::u32 total_samples, std::vector<eng::u8>& output) {
	using namespace eng::audio::acp1;
	if (payloads.empty() || payloads.size() > kMaxTracks || sample_rate == 0u || sample_rate > 65535u || total_samples == 0u) return false;
	eng::usize tracks_offset = kHeaderSize + payloads.size() * kUnitSize;
	std::vector<eng::u32> payload_offsets(payloads.size());
	for (eng::usize i = 0u; i < payloads.size(); ++i) {
		eng::audio::auzx::Header auzx_header {};
		if (!eng::audio::auzx::parse({payloads[i].data(), payloads[i].size()}, auzx_header) ||
			auzx_header.sample_rate != sample_rate || auzx_header.total_samples < total_samples ||
			payloads[i].size() > 0xffffffffu || tracks_offset > 0xffffffffu - payloads[i].size()) return false;
		payload_offsets[i] = static_cast<eng::u32>(tracks_offset);
		tracks_offset += payloads[i].size();
	}
	const eng::usize events_offset = tracks_offset + payloads.size() * kTrackSize;
	const eng::usize file_size = events_offset + payloads.size() * kEventSize;
	if (file_size > 0xffffffffu) return false;
	output.assign(file_size, 0u);
	eng::Span<eng::u8> file {output.data(), output.size()};
	file[0] = 'A'; file[1] = 'C'; file[2] = 'P'; file[3] = '1';
	wr16(file, 4u, 1u); wr32(file, 8u, sample_rate);
	wr16(file, 12u, static_cast<eng::u16>(payloads.size()));
	file[14] = static_cast<eng::u8>(payloads.size());
	wr32(file, 16u, total_samples); wr32(file, 24u, static_cast<eng::u32>(kHeaderSize));
	wr32(file, 28u, static_cast<eng::u32>(tracks_offset));
	wr32(file, 32u, static_cast<eng::u32>(events_offset));
	wr32(file, 36u, static_cast<eng::u32>(file_size));
	for (eng::usize i = 0u; i < payloads.size(); ++i) {
		const eng::usize unit_at = kHeaderSize + i * kUnitSize;
	wr32(file, unit_at, static_cast<eng::u32>(i));
		wr32(file, unit_at + 4u, payload_offsets[i]);
		wr32(file, unit_at + 8u, static_cast<eng::u32>(payloads[i].size()));
		wr32(file, unit_at + 12u, total_samples);
		file[unit_at + 16u] = 1u; file[unit_at + 18u] = 255u;
		for (eng::usize b = 0u; b < payloads[i].size(); ++b) file[static_cast<eng::usize>(payload_offsets[i]) + b] = payloads[i][b];
		const eng::usize track_at = tracks_offset + i * kTrackSize;
		file[track_at] = static_cast<eng::u8>(i); wr16(file, track_at + 2u, 1u);
		wr32(file, track_at + 4u, static_cast<eng::u32>(events_offset + i * kEventSize));
		const eng::usize event_at = events_offset + i * kEventSize;
		wr32(file, event_at, static_cast<eng::u32>(i));
		wr32(file, event_at + 8u, total_samples); file[event_at + 12u] = 255u;
	}
	Info parsed {};
	return parse({output.data(), output.size()}, parsed);
}

} // namespace audio_compressor
