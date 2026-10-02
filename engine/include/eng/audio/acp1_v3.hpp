#pragma once

/// \\file acp1_v3.hpp
/// Parser sin heap del MVP ACP1 v3: Units, Segments, PayloadBytes, Tracks y Events.
///
/// El contrato binario completo y sus secciones opcionales están descritos en
/// `docs/engine/architecture/AUDIO_COMPRESSION.md`. Este parser valida el directorio completo,
/// referencias y payloads del MVP; no intenta reproducir todavía síntesis, envolventes ni cues.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio::acp1_v3 {

inline constexpr eng::usize kHeaderSize = 64u;
inline constexpr eng::usize kSectionEntrySize = 16u;
inline constexpr eng::usize kSectionCount = 17u;
inline constexpr eng::usize kDirectoryOffset = 64u;
inline constexpr eng::usize kDirectorySize = kSectionCount * kSectionEntrySize;
inline constexpr eng::u16 kRequired = 1u;

inline constexpr eng::u16 kUnits = 1u;
inline constexpr eng::u16 kSegments = 2u;
inline constexpr eng::u16 kPayloadBytes = 3u;
inline constexpr eng::u16 kTracks = 4u;
inline constexpr eng::u16 kEvents = 5u;

inline constexpr eng::u32 kUnitSize = 24u;
inline constexpr eng::u32 kSegmentSize = 28u;
inline constexpr eng::u32 kTrackSize = 40u;
inline constexpr eng::u32 kEventSize = 44u;

struct Section {
	eng::u16 id = 0u;
	eng::u16 flags = 0u;
	eng::u32 entry_size = 0u;
	eng::u32 entry_count = 0u;
	eng::u32 offset = 0u;
};

struct Info {
	eng::u16 major = 0u;
	eng::u16 minor = 0u;
	eng::u32 sample_rate = 0u;
	eng::u64 timeline_samples = 0u;
	eng::u8 track_limit = 0u;
	eng::u8 required_paula_voices = 0u;
	eng::u8 required_mixer_voices = 0u;
	eng::u32 file_size = 0u;
	eng::u32 unit_count = 0u;
	eng::u32 segment_count = 0u;
	eng::u32 track_count = 0u;
	eng::u32 event_count = 0u;
	Section sections[kSectionCount] {};
};

/// Vista validada de una unidad ACP1 v3 y su representación.
struct Unit {
	eng::u32 id = 0u; ///< Índice consecutivo de la unidad.
	eng::u8 representation = 0u; ///< 0 PCM, 1 Additive o 2 Hybrid.
	eng::u8 flags = 0u; ///< bit 0: la unidad PCM puede repetirse durante un evento largo.
	eng::u32 decoded_samples = 0u; ///< Duración reconstruida en muestras.
	eng::u32 first_segment = 0xffffffffu; ///< Primer segmento o sentinel si es aditiva.
	eng::u16 segment_count = 0u; ///< Número de segmentos residuales.
	eng::u16 synthesis_index = 0xffffu; ///< Índice de SynthesisParams o sentinel.
	eng::u16 reference_gain_q8_8 = 256u; ///< Ganancia de referencia; 256 = unidad.
};

/// Vista de los parámetros de síntesis asociados a una unidad.
struct SynthesisParams {
	eng::u32 unit_id = 0u; ///< Unidad que referencia estos parámetros.
	eng::u32 fundamental_hz_q16_16 = 0u; ///< Fundamental base en Hz Q16.16.
	eng::u32 phase_q0_32 = 0u; ///< Fase inicial de la fundamental.
	eng::u32 first_partial = 0u; ///< Primer parcial asociado.
	eng::u16 partial_count = 0u; ///< Número de parciales asociados.
	eng::u8 waveform = 0u; ///< 0 seno, 1 wavetable, 2 triangular, 3 cuadrada.
	eng::u16 level_q8_8 = 256u; ///< Nivel de la suma; 256 = unidad.
	eng::u16 wave_table_id = 0xffffu; ///< Wavetable custom o sentinel para formas estándar.
};

/// Parcial armónico serializado en ACP1 v3.
struct Partial {
	eng::u16 ratio_q8_8 = 256u; ///< Relación respecto a la fundamental; 256 = 1x.
	eng::s16 amplitude_q1_15 = 0; ///< Amplitud firmada del parcial.
	eng::u32 phase_q0_32 = 0u; ///< Desfase del parcial.
};

/// Vista de una pista y sus restricciones de ruta.
struct Track {
	eng::u16 id = 0u; ///< Identificador de la pista.
	eng::u8 route = 0u; ///< Ruta ACP1 v3: Auto, Paula/Mixer preferido o requerido.
	eng::u32 first_event = 0u; ///< Primer evento de la pista.
	eng::u32 event_count = 0u; ///< Número de eventos de la pista.
	eng::u16 gain_envelope = 0xffffu; ///< Envolvente de ganancia o sentinel.
	eng::u16 pitch_envelope = 0xffffu; ///< Envolvente de pitch o sentinel.
	eng::u16 gain_q8_8 = 256u; ///< Ganancia fija de pista.
	eng::s8 pan_s8 = 0; ///< Paneo -127..127.
	eng::u8 priority = 0u; ///< Prioridad para la planificación de voces.
};

/// Vista de un evento de nota o unidad en la timeline común.
struct Event {
	eng::u16 track_id = 0u; ///< Pista propietaria.
	eng::u16 flags = 0u; ///< Flags v3, incluido CueOnly.
	eng::u64 start_sample = 0u; ///< Inicio en muestras de la timeline maestra.
	eng::u32 duration = 0u; ///< Duración del evento.
	eng::u32 unit_id = 0u; ///< Unidad aditiva, híbrida o PCM.
	eng::u32 unit_offset = 0u; ///< Desplazamiento dentro de la unidad.
	eng::u16 gain_q8_8 = 256u; ///< Ganancia del evento.
	eng::s16 pitch_semitones_q8_8 = 0; ///< Desplazamiento de pitch en semitonos Q8.8.
	eng::u16 gain_envelope = 0xffffu; ///< Envolvente de ganancia o sentinel.
	eng::u16 pitch_envelope = 0xffffu; ///< Envolvente de pitch o sentinel.
};

[[nodiscard]] inline eng::u16 rd16(eng::Span<const eng::u8> file, eng::usize at) noexcept {
	return static_cast<eng::u16>(file[at]) |
		static_cast<eng::u16>(static_cast<eng::u16>(file[at + 1u]) << 8u);
}

[[nodiscard]] inline eng::u32 rd32(eng::Span<const eng::u8> file, eng::usize at) noexcept {
	return static_cast<eng::u32>(file[at]) |
		(static_cast<eng::u32>(file[at + 1u]) << 8u) |
		(static_cast<eng::u32>(file[at + 2u]) << 16u) |
		(static_cast<eng::u32>(file[at + 3u]) << 24u);
}

[[nodiscard]] inline eng::u64 rd64(eng::Span<const eng::u8> file, eng::usize at) noexcept {
	return static_cast<eng::u64>(rd32(file, at)) |
		(static_cast<eng::u64>(rd32(file, at + 4u)) << 32u);
}

[[nodiscard]] inline bool extent_ok(const Section& section, eng::u32 file_size) noexcept {
	const eng::u64 extent = static_cast<eng::u64>(section.entry_size) * section.entry_count;
	return extent <= file_size && (section.entry_count == 0u
		? section.offset == 0u
		: section.offset >= kHeaderSize + kDirectorySize && section.offset <= file_size - extent);
}

[[nodiscard]] inline bool parse(eng::Span<const eng::u8> file, Info& out) noexcept {
	if (file.size() < kHeaderSize + kDirectorySize || file[0] != 'A' || file[1] != 'C' ||
		file[2] != 'P' || file[3] != '1' || rd16(file, 4u) != 3u || rd16(file, 6u) != 0u ||
		rd16(file, 8u) != kHeaderSize || rd16(file, 10u) != 0u || rd32(file, 32u) != kDirectoryOffset ||
		rd16(file, 24u) != kSectionCount || rd16(file, 42u) != 0u || rd32(file, 36u) != file.size()) return false;
	Info parsed {};
	parsed.major = 3u;
	parsed.minor = 0u;
	parsed.sample_rate = rd32(file, 12u);
	parsed.timeline_samples = rd64(file, 16u);
	parsed.track_limit = file[26u];
	parsed.required_paula_voices = file[27u];
	parsed.required_mixer_voices = file[28u];
	parsed.file_size = rd32(file, 36u);
	if (parsed.sample_rate == 0u || parsed.sample_rate > 65535u || parsed.timeline_samples == 0u ||
		parsed.track_limit == 0u || parsed.track_limit > 7u || parsed.required_paula_voices > 3u ||
		parsed.required_mixer_voices > 4u || rd32(file, 44u) != 0u) return false;
	for (eng::usize i = 0u; i < kSectionCount; ++i) {
		const eng::usize at = kDirectoryOffset + i * kSectionEntrySize;
		Section& section = parsed.sections[i];
		section.id = rd16(file, at);
		section.flags = rd16(file, at + 2u);
		section.entry_size = rd32(file, at + 4u);
		section.entry_count = rd32(file, at + 8u);
		section.offset = rd32(file, at + 12u);
		if (section.id != i + 1u || (section.flags & ~kRequired) != 0u || !extent_ok(section, parsed.file_size)) return false;
	}
	const auto& units = parsed.sections[kUnits - 1u];
	const auto& segments = parsed.sections[kSegments - 1u];
	const auto& payloads = parsed.sections[kPayloadBytes - 1u];
	const auto& tracks = parsed.sections[kTracks - 1u];
	const auto& events = parsed.sections[kEvents - 1u];
	const auto& synthesis = parsed.sections[8u];
	const auto& partials = parsed.sections[9u];
	if (units.entry_size != kUnitSize || segments.entry_size != kSegmentSize || tracks.entry_size != kTrackSize ||
		events.entry_size != kEventSize || payloads.entry_size != 1u || synthesis.entry_size != 28u || partials.entry_size != 8u || (units.flags & kRequired) == 0u ||
		(tracks.flags & kRequired) == 0u || (events.flags & kRequired) == 0u || units.entry_count == 0u ||
		tracks.entry_count == 0u || tracks.entry_count > 7u || events.entry_count == 0u ||
		((segments.entry_count != 0u) != ((segments.flags & kRequired) != 0u)) ||
		((payloads.entry_count != 0u) != ((payloads.flags & kRequired) != 0u))) return false;
	for (eng::usize i = 0u; i < kSectionCount; ++i) {
		const Section& a = parsed.sections[i];
		if (a.entry_count == 0u) continue;
		const eng::u64 a_end = static_cast<eng::u64>(a.offset) + static_cast<eng::u64>(a.entry_size) * a.entry_count;
		for (eng::usize j = i + 1u; j < kSectionCount; ++j) {
			const Section& b = parsed.sections[j];
			if (b.entry_count == 0u) continue;
			const eng::u64 b_end = static_cast<eng::u64>(b.offset) + static_cast<eng::u64>(b.entry_size) * b.entry_count;
			if (!(a_end <= b.offset || b_end <= a.offset)) return false;
		}
	}
	parsed.unit_count = units.entry_count;
	parsed.segment_count = segments.entry_count;
	parsed.track_count = tracks.entry_count;
	parsed.event_count = events.entry_count;
	for (eng::u32 i = 0u; i < parsed.unit_count; ++i) {
		const eng::usize at = units.offset + static_cast<eng::usize>(i) * kUnitSize;
		const eng::u8 representation = file[at + 4u];
		const eng::u16 synthesis_index = rd16(file, at + 16u);
		const eng::u16 segment_count = rd16(file, at + 14u);
		if (rd32(file, at) != i || representation > 2u || (file[at + 5u] & ~1u) != 0u || rd32(file, at + 20u) != 0u ||
			(representation == 0u && synthesis_index != 0xffffu) ||
			(representation != 0u && (synthesis_index >= synthesis.entry_count || segment_count != 0u || rd32(file, at + 10u) != 0xffffffffu))) return false;
		const eng::u32 decoded = rd32(file, at + 6u);
		const eng::u32 first = rd32(file, at + 10u);
		const eng::u16 count = rd16(file, at + 14u);
		if ((count == 0u) != (first == 0xffffffffu) || decoded == 0u) return false;
		if (static_cast<eng::u64>(first) + count > parsed.segment_count && count != 0u) return false;
	}
	for (eng::u32 i = 0u; i < synthesis.entry_count; ++i) {
		const eng::usize at = synthesis.offset + i * synthesis.entry_size;
		const eng::u32 first = rd32(file, at + 12u);
		const eng::u16 count = rd16(file, at + 16u);
		if (rd32(file, at) >= parsed.unit_count || file[at + 18u] == 1u || file[at + 18u] > 3u ||
			eng::u64 {first} + count > partials.entry_count) return false;
	}
	for (eng::u32 i = 0u; i < parsed.segment_count; ++i) {
		const eng::usize at = segments.offset + static_cast<eng::usize>(i) * kSegmentSize;
		const eng::u32 unit = rd32(file, at);
		const eng::u32 start = rd32(file, at + 4u);
		const eng::u32 count = rd32(file, at + 8u);
		const eng::u32 payload_at = rd32(file, at + 12u);
		const eng::u32 payload_size = rd32(file, at + 16u);
		if (unit >= parsed.unit_count || count == 0u || payload_size == 0u || rd16(file, at + 20u) > 7u ||
			file[at + 23u] != 0u || rd16(file, at + 26u) != 0u || payload_at < payloads.offset || payload_at > payloads.offset + payloads.entry_count ||
			payload_size > payloads.offset + payloads.entry_count - payload_at) return false;
		const eng::usize unit_at = units.offset + unit * kUnitSize;
		if (start > rd32(file, unit_at + 6u) || count > rd32(file, unit_at + 6u) - start) return false;
	}
	for (eng::u32 i = 0u; i < parsed.event_count; ++i) {
		const eng::usize at = events.offset + static_cast<eng::usize>(i) * kEventSize;
		const eng::u16 track = rd16(file, at);
		const eng::u32 duration = rd32(file, at + 12u);
		const eng::u32 unit = rd32(file, at + 16u);
		if (track >= parsed.track_count || rd16(file, at + 2u) != 0u || duration == 0u || unit >= parsed.unit_count ||
			rd16(file, at + 38u) != 0u || rd32(file, at + 40u) != 0u) return false;
		const eng::usize unit_at = units.offset + unit * kUnitSize;
		const eng::u32 unit_samples = rd32(file, unit_at + 6u);
		const eng::u32 unit_offset = rd32(file, at + 20u);
		const bool loopable_pcm = file[unit_at + 4u] == 0u && (file[unit_at + 5u] & 1u) != 0u;
		if (unit_offset > unit_samples || (!loopable_pcm && duration > unit_samples - unit_offset)) return false;
	}
	out = parsed;
	return true;
}

/// Lee una unidad validada de la tabla `Units` sin reservar memoria.
[[nodiscard]] inline bool unit(eng::Span<const eng::u8> file, const Info& info, eng::u32 index, Unit& out) noexcept {
	const Section& section = info.sections[kUnits - 1u];
	if (index >= section.entry_count || section.entry_size != kUnitSize) return false;
	const eng::usize at = section.offset + index * kUnitSize;
	out.id = rd32(file, at); out.representation = file[at + 4u]; out.decoded_samples = rd32(file, at + 6u);
	out.flags = file[at + 5u];
	out.first_segment = rd32(file, at + 10u); out.segment_count = rd16(file, at + 14u);
	out.synthesis_index = rd16(file, at + 16u); out.reference_gain_q8_8 = rd16(file, at + 18u);
	return out.id == index;
}

/// Lee los parámetros de síntesis de una unidad validada.
[[nodiscard]] inline bool synthesis(eng::Span<const eng::u8> file, const Info& info, eng::u32 index, SynthesisParams& out) noexcept {
	const Section& section = info.sections[8u];
	if (index >= section.entry_count || section.entry_size != 28u) return false;
	const eng::usize at = section.offset + index * 28u;
	out.unit_id = rd32(file, at); out.fundamental_hz_q16_16 = rd32(file, at + 4u); out.phase_q0_32 = rd32(file, at + 8u);
	out.first_partial = rd32(file, at + 12u); out.partial_count = rd16(file, at + 16u); out.waveform = file[at + 18u];
	out.level_q8_8 = rd16(file, at + 20u); out.wave_table_id = rd16(file, at + 22u);
	return true;
}

/// Lee un parcial armónico validado.
[[nodiscard]] inline bool partial(eng::Span<const eng::u8> file, const Info& info, eng::u32 index, Partial& out) noexcept {
	const Section& section = info.sections[9u];
	if (index >= section.entry_count || section.entry_size != 8u) return false;
	const eng::usize at = section.offset + index * 8u;
	out.ratio_q8_8 = rd16(file, at); out.amplitude_q1_15 = rd16(file, at + 2u); out.phase_q0_32 = rd32(file, at + 4u);
	return true;
}

/// Lee una pista y conserva sus restricciones de asignación para el planner.
[[nodiscard]] inline bool track(eng::Span<const eng::u8> file, const Info& info, eng::u32 index, Track& out) noexcept {
	const Section& section = info.sections[kTracks - 1u];
	if (index >= section.entry_count || section.entry_size != kTrackSize) return false;
	const eng::usize at = section.offset + index * kTrackSize;
	out.id = rd16(file, at); out.route = file[at + 2u]; out.first_event = rd32(file, at + 4u); out.event_count = rd32(file, at + 8u);
	out.gain_envelope = rd16(file, at + 12u); out.pitch_envelope = rd16(file, at + 14u); out.gain_q8_8 = rd16(file, at + 16u);
	out.pan_s8 = file[at + 18u]; out.priority = file[at + 19u];
	return out.id == index;
}

/// Lee un evento de nota validado y expone su pitch y ganancia sin interpretar hardware.
[[nodiscard]] inline bool event(eng::Span<const eng::u8> file, const Info& info, eng::u32 index, Event& out) noexcept {
	const Section& section = info.sections[kEvents - 1u];
	if (index >= section.entry_count || section.entry_size != kEventSize) return false;
	const eng::usize at = section.offset + index * kEventSize;
	out.track_id = rd16(file, at); out.flags = rd16(file, at + 2u); out.start_sample = rd64(file, at + 4u);
	out.duration = rd32(file, at + 12u); out.unit_id = rd32(file, at + 16u); out.unit_offset = rd32(file, at + 20u);
	out.gain_q8_8 = rd16(file, at + 24u); out.pitch_semitones_q8_8 = rd16(file, at + 28u);
	out.gain_envelope = rd16(file, at + 30u); out.pitch_envelope = rd16(file, at + 32u);
	return true;
}

} // namespace eng::audio::acp1_v3
