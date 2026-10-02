#pragma once

/// Serializador host-only del MVP ACP1 v3. Genera unidades PCM8 sin síntesis, envolventes ni cues.

#include <algorithm>
#include <cstring>
#include <vector>

#include <eng/audio/acp1_v3.hpp>
#include <eng/audio/synth_renderer.hpp>
#include "../include/audio_compressor/formats/binary.hpp"

namespace audio_compressor {

/// Nota aditiva host: intervalo, afinación relativa y ganancia de una unidad instrumental.
struct AdditiveNote {
	eng::u64 start_sample = 0u; ///< Inicio en la timeline maestra.
	eng::u32 duration = 0u; ///< Duración en muestras.
	eng::s16 pitch_semitones_q8_8 = 0; ///< Pitch relativo en semitonos Q8.8.
	eng::u16 gain_q8_8 = 256u; ///< Ganancia del evento; 256 = unidad.
};

/// Pista aditiva host con un timbre reutilizable y una secuencia de notas.
struct AdditiveTrack {
	eng::u8 route = 0u; ///< Ruta ACP1 v3: 0 Auto, 1/2 Prefer, 3/4 Required.
	eng::u32 fundamental_hz_q16_16 = 0u; ///< Fundamental base del timbre.
	eng::u32 phase_q0_32 = 0u; ///< Fase inicial del timbre.
	eng::u8 waveform = 0u; ///< 0 seno, 2 triangular, 3 cuadrada.
	eng::u16 level_q8_8 = 256u; ///< Nivel del timbre; 256 = unidad.
	std::vector<eng::audio::SynthPartial> partials; ///< Parciales armónicos del timbre.
	std::vector<AdditiveNote> notes; ///< Eventos ordenados y no solapados de la pista.
};

struct SpectralPcmEvent {
	eng::u64 start_sample = 0u;
	eng::u32 duration = 0u;
	eng::u16 gain_q8_8 = 256u;
	eng::s16 pitch_semitones_q8_8 = 0;
};

struct SpectralPcmTrack {
	eng::u8 route = 0u;
	std::vector<eng::u8> pcm;
	std::vector<SpectralPcmEvent> events;
};

inline void v3_wr16(std::vector<eng::u8>& file, eng::usize at, eng::u16 value) {
	(void)formats::BinaryWriter {file}.u16(at, value);
}

inline void v3_wr32(std::vector<eng::u8>& file, eng::usize at, eng::u32 value) {
	(void)formats::BinaryWriter {file}.u32(at, value);
}

inline void v3_wr64(std::vector<eng::u8>& file, eng::usize at, eng::u64 value) {
	(void)formats::BinaryWriter {file}.u64(at, value);
}

[[nodiscard]] inline eng::usize v3_align4(eng::usize value) noexcept;

/// Escribe un diccionario ACP1 v3 compacto: cada prototipo PCM es una unidad compartida por eventos.
[[nodiscard]] inline bool build_acp1_v3_spectral(const std::vector<SpectralPcmTrack>& tracks,
	eng::u16 sample_rate, eng::u64 timeline_samples, std::vector<eng::u8>& output) {
	using namespace eng::audio::acp1_v3;
	if (tracks.empty() || tracks.size() > 7u || sample_rate == 0u || timeline_samples == 0u || timeline_samples > 0xffffffffu) return false;
	eng::usize event_count = 0u; eng::usize payload_bytes = 0u; eng::u8 paula = 0u; eng::u8 mixer = 0u;
	for (const auto& track : tracks) {
		if (track.pcm.empty() || track.pcm.size() > 0xffffffffu || track.events.empty() || track.route > 4u) return false;
		if (track.route == 3u) ++paula;
		if (track.route == 4u) ++mixer;
		if (payload_bytes > 0xffffffffu - track.pcm.size()) return false;
		payload_bytes += track.pcm.size(); event_count += track.events.size();
		for (const auto& event : track.events) if (event.duration == 0u || event.duration > track.pcm.size() || event.start_sample > timeline_samples - event.duration) return false;
	}
	if (event_count == 0u || event_count > 0xffffffffu) return false;
	const eng::usize units_offset = kHeaderSize + kDirectorySize;
	const eng::usize segments_offset = v3_align4(units_offset + tracks.size() * kUnitSize);
	const eng::usize payload_offset = v3_align4(segments_offset + tracks.size() * kSegmentSize);
	const eng::usize tracks_offset = v3_align4(payload_offset + payload_bytes);
	const eng::usize events_offset = v3_align4(tracks_offset + tracks.size() * kTrackSize);
	const eng::usize file_size = events_offset + event_count * kEventSize;
	if (file_size > 0xffffffffu) return false;
	output.assign(file_size, 0u);
	output[0] = 'A'; output[1] = 'C'; output[2] = 'P'; output[3] = '1';
	v3_wr16(output, 4u, 3u); v3_wr16(output, 8u, kHeaderSize); v3_wr32(output, 12u, sample_rate); v3_wr64(output, 16u, timeline_samples);
	v3_wr16(output, 24u, kSectionCount); output[26] = static_cast<eng::u8>(tracks.size()); output[27] = paula; output[28] = mixer; v3_wr32(output, 32u, kDirectoryOffset); v3_wr32(output, 36u, static_cast<eng::u32>(file_size)); v3_wr16(output, 30u, 256u);
	const eng::u32 sizes[] = {kUnitSize, kSegmentSize, 1u, kTrackSize, kEventSize, 16u, 12u, 34u, 28u, 8u, 16u, 1u, 16u, 1u, 16u, 1u, 1u};
	const eng::u32 counts[] = {static_cast<eng::u32>(tracks.size()), static_cast<eng::u32>(tracks.size()), static_cast<eng::u32>(payload_bytes), static_cast<eng::u32>(tracks.size()), static_cast<eng::u32>(event_count), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const eng::u32 offsets[] = {static_cast<eng::u32>(units_offset), static_cast<eng::u32>(segments_offset), static_cast<eng::u32>(payload_offset), static_cast<eng::u32>(tracks_offset), static_cast<eng::u32>(events_offset), 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	for (eng::usize i = 0u; i < kSectionCount; ++i) { const eng::usize at = kDirectoryOffset + i * kSectionEntrySize; v3_wr16(output, at, static_cast<eng::u16>(i + 1u)); v3_wr16(output, at + 2u, i < 5u ? kRequired : 0u); v3_wr32(output, at + 4u, sizes[i]); v3_wr32(output, at + 8u, counts[i]); v3_wr32(output, at + 12u, offsets[i]); }
	eng::usize payload_cursor = 0u, event_cursor = 0u;
	for (eng::usize track_id = 0u; track_id < tracks.size(); ++track_id) {
		const auto& track = tracks[track_id]; const eng::usize unit_at = units_offset + track_id * kUnitSize; const eng::usize segment_at = segments_offset + track_id * kSegmentSize; const eng::usize track_at = tracks_offset + track_id * kTrackSize;
		v3_wr32(output, unit_at, static_cast<eng::u32>(track_id)); output[unit_at + 4u] = 0u; v3_wr32(output, unit_at + 6u, static_cast<eng::u32>(track.pcm.size())); v3_wr32(output, unit_at + 10u, static_cast<eng::u32>(track_id)); v3_wr16(output, unit_at + 14u, 1u); v3_wr16(output, unit_at + 16u, 0xffffu); v3_wr16(output, unit_at + 18u, 256u);
		v3_wr32(output, segment_at, static_cast<eng::u32>(track_id)); v3_wr32(output, segment_at + 4u, 0u); v3_wr32(output, segment_at + 8u, static_cast<eng::u32>(track.pcm.size())); v3_wr32(output, segment_at + 12u, static_cast<eng::u32>(payload_offset + payload_cursor)); v3_wr32(output, segment_at + 16u, static_cast<eng::u32>(track.pcm.size())); v3_wr16(output, segment_at + 20u, 0u); std::memcpy(output.data() + payload_cursor + payload_offset, track.pcm.data(), track.pcm.size()); payload_cursor += track.pcm.size();
		v3_wr16(output, track_at, static_cast<eng::u16>(track_id)); output[track_at + 2u] = track.route; v3_wr32(output, track_at + 4u, static_cast<eng::u32>(event_cursor)); v3_wr32(output, track_at + 8u, static_cast<eng::u32>(track.events.size())); v3_wr16(output, track_at + 12u, 0xffffu); v3_wr16(output, track_at + 14u, 0xffffu); v3_wr16(output, track_at + 16u, 256u);
		for (const auto& event : track.events) { const eng::usize event_at = events_offset + event_cursor++ * kEventSize; v3_wr16(output, event_at, static_cast<eng::u16>(track_id)); v3_wr64(output, event_at + 4u, event.start_sample); v3_wr32(output, event_at + 12u, event.duration); v3_wr32(output, event_at + 16u, static_cast<eng::u32>(track_id)); v3_wr32(output, event_at + 20u, 0u); v3_wr16(output, event_at + 24u, event.gain_q8_8); v3_wr16(output, event_at + 28u, static_cast<eng::u16>(event.pitch_semitones_q8_8)); v3_wr16(output, event_at + 30u, 0xffffu); v3_wr16(output, event_at + 32u, 0xffffu); }
	}
	Info parsed {}; return parse({output.data(), output.size()}, parsed);
}

[[nodiscard]] inline eng::usize v3_align4(eng::usize value) noexcept { return (value + 3u) & ~eng::usize {3u}; }

/// Escribe ACP1 v3 exclusivamente aditivo: cada pista aporta un timbre y eventos de nota.
[[nodiscard]] inline bool build_acp1_v3_additive(const std::vector<AdditiveTrack>& tracks,
	eng::u16 sample_rate, std::vector<eng::u8>& output) {
	using namespace eng::audio::acp1_v3;
	if (tracks.empty() || tracks.size() > 7u || sample_rate == 0u) return false;
	eng::u64 timeline = 0u;
	eng::usize event_count = 0u;
	eng::usize partial_count = 0u;
	for (const AdditiveTrack& track : tracks) {
		if (track.route > 4u || track.fundamental_hz_q16_16 == 0u || track.partials.empty() || track.partials.size() > 65535u || track.notes.empty()) return false;
		eng::u64 previous_end = 0u;
		for (const AdditiveNote& note : track.notes) {
			if (note.duration == 0u || note.start_sample < previous_end || note.start_sample > 0xffffffffffffffffull - note.duration) return false;
			previous_end = note.start_sample + note.duration;
			if (previous_end > timeline) timeline = previous_end;
		}
		event_count += track.notes.size(); partial_count += track.partials.size();
	}
	if (timeline == 0u || event_count > 0xffffffffu || partial_count > 0xffffffffu) return false;
	const eng::usize units_offset = kHeaderSize + kDirectorySize;
	const eng::usize tracks_offset = v3_align4(units_offset + tracks.size() * kUnitSize);
	const eng::usize events_offset = v3_align4(tracks_offset + tracks.size() * kTrackSize);
	const eng::usize synthesis_offset = v3_align4(events_offset + event_count * kEventSize);
	const eng::usize partials_offset = v3_align4(synthesis_offset + tracks.size() * 28u);
	const eng::usize file_size = v3_align4(partials_offset + partial_count * 8u);
	if (file_size > 0xffffffffu) return false;
	output.assign(file_size, 0u);
	output[0] = 'A'; output[1] = 'C'; output[2] = 'P'; output[3] = '1';
	v3_wr16(output, 4u, 3u); v3_wr16(output, 8u, kHeaderSize); v3_wr32(output, 12u, sample_rate);
	v3_wr64(output, 16u, timeline); v3_wr16(output, 24u, kSectionCount); output[26] = static_cast<eng::u8>(tracks.size());
	v3_wr32(output, 32u, kDirectoryOffset); v3_wr32(output, 36u, static_cast<eng::u32>(file_size));
	v3_wr16(output, 30u, 256u);
	const eng::u32 sizes[] = {kUnitSize, kSegmentSize, 1u, kTrackSize, kEventSize, 16u, 12u, 34u, 28u, 8u, 16u, 1u, 16u, 1u, 16u, 1u, 1u};
	const eng::u32 counts[] = {static_cast<eng::u32>(tracks.size()), 0u, 0u, static_cast<eng::u32>(tracks.size()), static_cast<eng::u32>(event_count), 0u, 0u, 0u, static_cast<eng::u32>(tracks.size()), static_cast<eng::u32>(partial_count), 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	const eng::u32 offsets[] = {static_cast<eng::u32>(units_offset), 0u, 0u, static_cast<eng::u32>(tracks_offset), static_cast<eng::u32>(events_offset), 0u, 0u, 0u, static_cast<eng::u32>(synthesis_offset), static_cast<eng::u32>(partials_offset), 0u, 0u, 0u, 0u, 0u, 0u, 0u};
	for (eng::usize i = 0u; i < kSectionCount; ++i) {
		const eng::usize at = kDirectoryOffset + i * kSectionEntrySize;
		v3_wr16(output, at, static_cast<eng::u16>(i + 1u));
		const bool required = i == 0u || i == 3u || i == 4u || i == 8u || i == 9u;
		v3_wr16(output, at + 2u, required ? kRequired : 0u); v3_wr32(output, at + 4u, sizes[i]);
		v3_wr32(output, at + 8u, counts[i]); v3_wr32(output, at + 12u, offsets[i]);
	}
	eng::usize event_id = 0u;
	eng::usize partial_id = 0u;
	for (eng::usize track_id = 0u; track_id < tracks.size(); ++track_id) {
		const AdditiveTrack& track = tracks[track_id];
		const eng::usize unit_at = units_offset + track_id * kUnitSize;
		eng::u32 max_duration = 0u;
		for (const AdditiveNote& note : track.notes) if (note.duration > max_duration) max_duration = note.duration;
		v3_wr32(output, unit_at, static_cast<eng::u32>(track_id)); output[unit_at + 4u] = 1u;
		v3_wr32(output, unit_at + 6u, max_duration); v3_wr32(output, unit_at + 10u, 0xffffffffu);
		v3_wr16(output, unit_at + 14u, 0u); v3_wr16(output, unit_at + 16u, static_cast<eng::u16>(track_id)); v3_wr16(output, unit_at + 18u, 256u);
		const eng::usize track_at = tracks_offset + track_id * kTrackSize;
		v3_wr16(output, track_at, static_cast<eng::u16>(track_id)); output[track_at + 2u] = track.route;
		v3_wr32(output, track_at + 4u, static_cast<eng::u32>(event_id)); v3_wr32(output, track_at + 8u, static_cast<eng::u32>(track.notes.size()));
		v3_wr16(output, track_at + 12u, 0xffffu); v3_wr16(output, track_at + 14u, 0xffffu); v3_wr16(output, track_at + 16u, 256u);
		const eng::usize synthesis_at = synthesis_offset + track_id * 28u;
		v3_wr32(output, synthesis_at, static_cast<eng::u32>(track_id)); v3_wr32(output, synthesis_at + 4u, track.fundamental_hz_q16_16);
		v3_wr32(output, synthesis_at + 8u, track.phase_q0_32); v3_wr32(output, synthesis_at + 12u, static_cast<eng::u32>(partial_id));
		v3_wr16(output, synthesis_at + 16u, static_cast<eng::u16>(track.partials.size())); output[synthesis_at + 18u] = track.waveform;
		v3_wr16(output, synthesis_at + 20u, track.level_q8_8); v3_wr16(output, synthesis_at + 22u, 0xffffu);
		for (const eng::audio::SynthPartial& partial : track.partials) {
			const eng::usize partial_at = partials_offset + partial_id * 8u;
			v3_wr16(output, partial_at, partial.ratio_q8_8); v3_wr16(output, partial_at + 2u, partial.amplitude_q1_15);
			v3_wr32(output, partial_at + 4u, partial.phase_q0_32); ++partial_id;
		}
		for (const AdditiveNote& note : track.notes) {
			const eng::usize event_at = events_offset + event_id * kEventSize;
			v3_wr16(output, event_at, static_cast<eng::u16>(track_id)); v3_wr64(output, event_at + 4u, note.start_sample);
			v3_wr32(output, event_at + 12u, note.duration); v3_wr32(output, event_at + 16u, static_cast<eng::u32>(track_id));
			v3_wr16(output, event_at + 24u, note.gain_q8_8); v3_wr16(output, event_at + 28u, note.pitch_semitones_q8_8);
			v3_wr16(output, event_at + 30u, 0xffffu); v3_wr16(output, event_at + 32u, 0xffffu); ++event_id;
		}
	}
	Info parsed {};
	return parse({output.data(), output.size()}, parsed);
}

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
