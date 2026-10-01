#pragma once

/// Serializador host-only del MVP ACP1 v3. Genera unidades PCM8 sin síntesis, envolventes ni cues.

#include <algorithm>
#include <cstring>
#include <vector>

#include <eng/audio/acp1_v3.hpp>

namespace audio_compressor {

inline void v3_wr16(std::vector<eng::u8>& file, eng::usize at, eng::u16 value) {
	file[at] = static_cast<eng::u8>(value);
	file[at + 1u] = static_cast<eng::u8>(value >> 8u);
}

inline void v3_wr32(std::vector<eng::u8>& file, eng::usize at, eng::u32 value) {
	file[at] = static_cast<eng::u8>(value);
	file[at + 1u] = static_cast<eng::u8>(value >> 8u);
	file[at + 2u] = static_cast<eng::u8>(value >> 16u);
	file[at + 3u] = static_cast<eng::u8>(value >> 24u);
}

inline void v3_wr64(std::vector<eng::u8>& file, eng::usize at, eng::u64 value) {
	v3_wr32(file, at, static_cast<eng::u32>(value));
	v3_wr32(file, at + 4u, static_cast<eng::u32>(value >> 32u));
}

[[nodiscard]] inline eng::usize v3_align4(eng::usize value) noexcept { return (value + 3u) & ~eng::usize {3u}; }

/// Construye ACP1 v3 con una unidad PCM por bloque y eventos secuenciales por stem.
[[nodiscard]] inline bool build_acp1_v3(const std::vector<std::vector<eng::u8>>& stems,
	eng::u16 sample_rate, eng::u16 chunk_samples, std::vector<eng::u8>& output) {
	using namespace eng::audio::acp1_v3;
	if (stems.empty() || stems.size() > 7u || sample_rate == 0u || chunk_samples == 0u) return false;
	const eng::usize samples = stems.front().size();
	if (samples == 0u || samples > 0xffffffffu) return false;
	for (const auto& stem : stems) if (stem.size() != samples) return false;
	const eng::usize blocks = (samples + chunk_samples - 1u) / chunk_samples;
	const eng::usize units = stems.size() * blocks;
	const eng::usize events = units;
	if (units == 0u || units > 0xffffffffu || events > 0xffffffffu) return false;
	const eng::usize unit_bytes = units * kUnitSize;
	const eng::usize segment_bytes = units * kSegmentSize;
	const eng::usize payload_bytes = units * static_cast<eng::usize>(chunk_samples);
	const eng::usize track_bytes = stems.size() * kTrackSize;
	const eng::usize event_bytes = events * kEventSize;
	eng::usize cursor = kHeaderSize + kDirectorySize;
	const eng::usize units_offset = cursor; cursor += unit_bytes;
	cursor = v3_align4(cursor); const eng::usize segments_offset = cursor; cursor += segment_bytes;
	cursor = v3_align4(cursor); const eng::usize payload_offset = cursor; cursor += payload_bytes;
	cursor = v3_align4(cursor); const eng::usize tracks_offset = cursor; cursor += track_bytes;
	cursor = v3_align4(cursor); const eng::usize events_offset = cursor; cursor += event_bytes;
	if (cursor > 0xffffffffu) return false;
	output.assign(cursor, 0u);
	output[0] = 'A'; output[1] = 'C'; output[2] = 'P'; output[3] = '1';
	v3_wr16(output, 4u, 3u); v3_wr16(output, 8u, kHeaderSize); v3_wr32(output, 12u, sample_rate);
	v3_wr64(output, 16u, samples); v3_wr16(output, 24u, kSectionCount); output[26] = static_cast<eng::u8>(stems.size());
	v3_wr32(output, 32u, kDirectoryOffset);
	v3_wr32(output, 36u, static_cast<eng::u32>(output.size()));
	const eng::u32 sizes[] = {kUnitSize, kSegmentSize, 1u, kTrackSize, kEventSize, 16u, 12u, 34u, 28u, 8u, 16u, 1u, 16u, 1u, 16u, 1u, 1u};
	const eng::u32 counts[] = {static_cast<eng::u32>(units), static_cast<eng::u32>(units), static_cast<eng::u32>(payload_bytes),
		static_cast<eng::u32>(stems.size()), static_cast<eng::u32>(events), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const eng::u32 offsets[] = {static_cast<eng::u32>(units_offset), static_cast<eng::u32>(segments_offset), static_cast<eng::u32>(payload_offset),
		static_cast<eng::u32>(tracks_offset), static_cast<eng::u32>(events_offset), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	for (eng::usize i = 0u; i < kSectionCount; ++i) {
		const eng::usize at = kDirectoryOffset + i * kSectionEntrySize;
		v3_wr16(output, at, static_cast<eng::u16>(i + 1u));
		const bool required = i < 5u;
		v3_wr16(output, at + 2u, required ? kRequired : 0u);
		v3_wr32(output, at + 4u, sizes[i]); v3_wr32(output, at + 8u, counts[i]); v3_wr32(output, at + 12u, offsets[i]);
	}
	eng::usize unit_id = 0u;
	eng::usize event_id = 0u;
	for (eng::usize track = 0u; track < stems.size(); ++track) {
		const eng::usize track_at = tracks_offset + track * kTrackSize;
		v3_wr16(output, track_at, static_cast<eng::u16>(track)); v3_wr32(output, track_at + 4u, static_cast<eng::u32>(event_id));
		v3_wr32(output, track_at + 8u, static_cast<eng::u32>(blocks)); v3_wr16(output, track_at + 12u, 0xffffu);
		v3_wr16(output, track_at + 14u, 0xffffu); v3_wr16(output, track_at + 16u, 256u);
		for (eng::usize block = 0u; block < blocks; ++block, ++unit_id, ++event_id) {
			const eng::usize start = block * chunk_samples;
			const eng::u32 count = static_cast<eng::u32>(std::min<eng::usize>(chunk_samples, samples - start));
			const eng::usize unit_at = units_offset + unit_id * kUnitSize;
			v3_wr32(output, unit_at, static_cast<eng::u32>(unit_id)); output[unit_at + 4u] = 0u;
			v3_wr32(output, unit_at + 6u, count); v3_wr32(output, unit_at + 10u, static_cast<eng::u32>(unit_id));
			v3_wr16(output, unit_at + 14u, 1u); v3_wr16(output, unit_at + 16u, 0xffffu); v3_wr16(output, unit_at + 18u, 256u);
			const eng::usize segment_at = segments_offset + unit_id * kSegmentSize;
			v3_wr32(output, segment_at, static_cast<eng::u32>(unit_id)); v3_wr32(output, segment_at + 8u, count);
			v3_wr32(output, segment_at + 4u, 0u); v3_wr32(output, segment_at + 12u, static_cast<eng::u32>(payload_offset + unit_id * chunk_samples));
			v3_wr32(output, segment_at + 16u, count); v3_wr16(output, segment_at + 20u, 0u);
			const eng::usize event_at = events_offset + event_id * kEventSize;
			v3_wr16(output, event_at, static_cast<eng::u16>(track)); v3_wr64(output, event_at + 4u, start);
			v3_wr32(output, event_at + 12u, count); v3_wr32(output, event_at + 16u, static_cast<eng::u32>(unit_id));
			v3_wr16(output, event_at + 24u, 256u); v3_wr16(output, event_at + 28u, 0xffffu); v3_wr16(output, event_at + 30u, 0xffffu);
			std::memcpy(output.data() + payload_offset + unit_id * chunk_samples, stems[track].data() + start, count);
		}
	}
	Info parsed {};
	return parse({output.data(), output.size()}, parsed);
}

} // namespace audio_compressor
