#pragma once

/// \file acp1_writer.hpp
/// Serializador host-only de stems AUZX al formato ACP1 v2.

#include <vector>
#include <cstring>

#include <eng/audio/acp1.hpp>

namespace audio_compressor {

/// Serializa payloads AUZX y eventos por track; devuelve `false` ante unidades o timelines inválidas.
[[nodiscard]] inline bool build_acp1(const std::vector<std::vector<eng::u8>>& payloads,
	eng::u32 sample_rate, eng::u32 total_samples, std::vector<eng::u8>& output,
	const std::vector<std::vector<eng::audio::acp1::Event>>& track_events = {}) {
	using namespace eng::audio::acp1;
	const eng::usize track_count = track_events.empty() ? payloads.size() : track_events.size();
	if (payloads.empty() || payloads.size() > 65535u || track_count == 0u || track_count > kMaxTracks ||
		sample_rate == 0u || sample_rate > 65535u) return false;
	std::vector<std::vector<eng::audio::acp1::Event>> resolved_events(track_count);
	const eng::u32 timeline_samples = track_events.empty() ? total_samples : [&] {
		eng::u32 extent = 0u;
		for (const auto& track : track_events) for (const auto& event : track) {
			if (event.start_sample <= 0xffffffffu - event.duration && event.start_sample + event.duration > extent)
				extent = event.start_sample + event.duration;
		}
		return extent;
	}();
	if (timeline_samples == 0u || (track_events.empty() && timeline_samples != total_samples)) return false;
	for (eng::usize track = 0u; track < track_count; ++track) {
		if (track_events.empty()) {
			resolved_events[track].push_back({static_cast<eng::u32>(track), 0u, timeline_samples, 255u});
		} else {
			resolved_events[track] = track_events[track];
		}
		if (resolved_events[track].empty() || resolved_events[track].size() > 65535u) return false;
		eng::u32 previous_end = 0u;
		for (const auto& event : resolved_events[track]) {
			if (event.unit_id >= payloads.size() || event.duration == 0u || event.start_sample < previous_end ||
				event.start_sample > timeline_samples || event.duration > timeline_samples - event.start_sample) return false;
			previous_end = event.start_sample + event.duration;
		}
	}
	std::vector<eng::u16> track_units(payloads.size());
	std::vector<eng::usize> unique_tracks;
	std::vector<eng::u32> payload_offsets;
	std::vector<eng::u32> payload_sizes;
	std::vector<eng::u32> decoded_samples;
	for (eng::usize i = 0u; i < payloads.size(); ++i) {
		eng::audio::auzx::Header auzx_header {};
		if (!eng::audio::auzx::parse({payloads[i].data(), payloads[i].size()}, auzx_header) ||
			auzx_header.sample_rate != sample_rate ||
			payloads[i].size() > 0xffffffffu) return false;
		eng::usize unit = 0u;
		for (; unit < unique_tracks.size(); ++unit) {
			const auto& candidate = payloads[unique_tracks[unit]];
			if (decoded_samples[unit] == auzx_header.total_samples && candidate.size() == payloads[i].size() &&
				std::memcmp(candidate.data(), payloads[i].data(), candidate.size()) == 0) break;
		}
		if (unit == unique_tracks.size()) {
			if (unit >= 65535u) return false;
			unique_tracks.push_back(i);
			payload_offsets.push_back(0u);
			payload_sizes.push_back(static_cast<eng::u32>(payloads[i].size()));
			decoded_samples.push_back(auzx_header.total_samples);
		}
		track_units[i] = static_cast<eng::u16>(unit);
	}
	eng::usize tracks_offset = kHeaderSize + unique_tracks.size() * kUnitSize;
	for (eng::usize unit = 0u; unit < unique_tracks.size(); ++unit) {
		if (tracks_offset > 0xffffffffu - payload_sizes[unit]) return false;
		payload_offsets[unit] = static_cast<eng::u32>(tracks_offset);
		tracks_offset += payload_sizes[unit];
	}
	eng::usize event_count = 0u;
	for (const auto& events : resolved_events) event_count += events.size();
	if (event_count > 0xffffffffu / kEventSize) return false;
	const eng::usize events_offset = tracks_offset + track_count * kTrackSize;
	const eng::usize file_size = events_offset + event_count * kEventSize;
	if (file_size > 0xffffffffu) return false;
	output.assign(file_size, 0u);
	eng::Span<eng::u8> file {output.data(), output.size()};
	file[0] = 'A'; file[1] = 'C'; file[2] = 'P'; file[3] = '1';
	wr16(file, 4u, 2u); wr32(file, 8u, sample_rate);
	wr16(file, 12u, static_cast<eng::u16>(unique_tracks.size()));
	file[14] = static_cast<eng::u8>(track_count);
	wr32(file, 16u, timeline_samples); wr32(file, 24u, static_cast<eng::u32>(kHeaderSize));
	wr32(file, 28u, static_cast<eng::u32>(tracks_offset));
	wr32(file, 32u, static_cast<eng::u32>(events_offset));
	wr32(file, 36u, static_cast<eng::u32>(file_size));
	for (eng::usize i = 0u; i < unique_tracks.size(); ++i) {
		const eng::usize unit_at = kHeaderSize + i * kUnitSize;
	wr32(file, unit_at, static_cast<eng::u32>(i));
		wr32(file, unit_at + 4u, payload_offsets[i]);
		wr32(file, unit_at + 8u, payload_sizes[i]);
		wr32(file, unit_at + 12u, decoded_samples[i]);
		file[unit_at + 16u] = 1u; file[unit_at + 18u] = 255u;
		const auto& payload = payloads[unique_tracks[i]];
		for (eng::usize b = 0u; b < payload.size(); ++b) file[static_cast<eng::usize>(payload_offsets[i]) + b] = payload[b];
	}
	eng::usize track_event_cursor = events_offset;
	for (eng::usize i = 0u; i < track_count; ++i) {
		const eng::usize track_at = tracks_offset + i * kTrackSize;
		file[track_at] = static_cast<eng::u8>(i); wr16(file, track_at + 2u, static_cast<eng::u16>(resolved_events[i].size()));
		wr32(file, track_at + 4u, static_cast<eng::u32>(track_event_cursor));
		for (const auto& event : resolved_events[i]) {
			const eng::usize event_at = track_event_cursor;
			wr32(file, event_at, track_units[event.unit_id]);
			wr32(file, event_at + 4u, event.start_sample);
			wr32(file, event_at + 8u, event.duration);
			file[event_at + 12u] = event.gain;
			track_event_cursor += kEventSize;
		}
	}
	Info parsed {};
	return parse({output.data(), output.size()}, parsed);
}

} // namespace audio_compressor
