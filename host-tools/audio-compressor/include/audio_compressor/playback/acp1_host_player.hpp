#pragma once

/// Player host de ACP1 v1/v2 y ACP1 v3 aditivo sobre ventanas, sin ownership del blob ni buffers.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <eng/audio/acp1_v3.hpp>
#include <eng/audio/media.hpp>
#include <eng/audio/pcm_codec.hpp>
#include <eng/audio/synth_renderer.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace audio_compressor::playback {

class Acp1HostPlayer {
public:
	/// Reconoce un blob ACP1 y conserva solo una vista validada del archivo.
	[[nodiscard]] bool open(eng::Span<const eng::u8> blob) noexcept {
		eng::audio::media::Info parsed {};
		if (!eng::audio::media::open(blob, parsed) || parsed.container != eng::audio::media::Container::Acp1) {
			eng::audio::acp1_v3::Info v3 {};
			if (!eng::audio::acp1_v3::parse(blob, v3)) return false;
			m_blob = blob; m_v3_info = v3; m_is_v3 = true; return true;
		}
		m_blob = blob;
		m_info = parsed;
		m_is_v3 = false;
		return true;
	}

	/// Devuelve la frecuencia común de la composición validada.
	[[nodiscard]] eng::u16 sample_rate() const noexcept { return static_cast<eng::u16>(m_is_v3 ? m_v3_info.sample_rate : m_info.sample_rate); }
	/// Devuelve la duración común de la composición validada.
	[[nodiscard]] eng::u32 total_samples() const noexcept { return m_is_v3 ? static_cast<eng::u32>(m_v3_info.timeline_samples) : m_info.total_samples; }
	/// Mezcla una ventana ACP1 usando scratch y acumulador prestados por el llamador.
	[[nodiscard]] eng::s32 read_window(eng::u32 first_sample, eng::Span<eng::u8> output,
		eng::Span<eng::u8> scratch, eng::Span<eng::s16> accumulator) const noexcept {
		if (!m_is_v3) return eng::audio::media::mix_window(m_blob, m_info, first_sample, output, scratch, accumulator);
		return read_v3_window(first_sample, output, scratch, accumulator, -1);
	}

	/// Renderiza solo una pista aditiva ACP1 v3 para escucharla o medir su fuga respecto a otras.
	[[nodiscard]] eng::s32 read_track_window(eng::u32 track_id, eng::u32 first_sample, eng::Span<eng::u8> output,
		eng::Span<eng::u8> scratch, eng::Span<eng::s16> accumulator) const noexcept {
		if (!m_is_v3 || track_id >= m_v3_info.track_count) return -1;
		return read_v3_window(first_sample, output, scratch, accumulator, static_cast<eng::s16>(track_id));
	}

private:
	/// Renderiza eventos aditivos ACP1 v3 en un acumulador host; el mismo algoritmo se reutiliza como referencia del player Amiga.
	[[nodiscard]] eng::s32 read_v3_window(eng::u32 first_sample, eng::Span<eng::u8> output,
		eng::Span<eng::u8> scratch, eng::Span<eng::s16> accumulator, eng::s16 track_filter) const noexcept {
		if (output.empty() || scratch.size() < output.size() || accumulator.size() < output.size() ||
			static_cast<eng::u64>(first_sample) >= m_v3_info.timeline_samples) return -1;
		const eng::usize count = std::min<eng::u64>(output.size(), m_v3_info.timeline_samples - first_sample);
		for (eng::usize i = 0u; i < count; ++i) accumulator[i] = 0;
		for (eng::u32 event_index = 0u; event_index < m_v3_info.event_count; ++event_index) {
			eng::audio::acp1_v3::Event event {};
			if (!eng::audio::acp1_v3::event(m_blob, m_v3_info, event_index, event)) return -1;
			if (track_filter >= 0 && event.track_id != static_cast<eng::u16>(track_filter)) continue;
			const eng::u64 event_end = event.start_sample + event.duration;
			const eng::u64 window_end = static_cast<eng::u64>(first_sample) + count;
			if (event_end <= first_sample || event.start_sample >= window_end) continue;
			eng::audio::acp1_v3::Track track {};
			eng::audio::acp1_v3::Unit unit {};
			if (!eng::audio::acp1_v3::unit(m_blob, m_v3_info, event.unit_id, unit) || !eng::audio::acp1_v3::track(m_blob, m_v3_info, event.track_id, track)) return -1;
			if (unit.representation == 0u) {
				if (unit.segment_count == 0u || unit.first_segment >= m_v3_info.segment_count) return -1;
				const auto& section = m_v3_info.sections[eng::audio::acp1_v3::kSegments - 1u];
				const eng::usize segment_at = section.offset + static_cast<eng::usize>(unit.first_segment) * section.entry_size;
				if (section.entry_size != eng::audio::acp1_v3::kSegmentSize || eng::audio::acp1_v3::rd32(m_blob, segment_at) != unit.id || eng::audio::acp1_v3::rd16(m_blob, segment_at + 20u) > 7u) return -1;
				const eng::u32 payload_offset = eng::audio::acp1_v3::rd32(m_blob, segment_at + 12u);
				const eng::u32 payload_size = eng::audio::acp1_v3::rd32(m_blob, segment_at + 16u);
				const eng::u16 acp_codec = eng::audio::acp1_v3::rd16(m_blob, segment_at + 20u);
				const eng::u64 payload_end = static_cast<eng::u64>(payload_offset) + payload_size;
				if (payload_end > m_blob.size() || (acp_codec == 0u && payload_size < unit.decoded_samples)) return -1;
				const eng::u8 compression = acp_codec == 0u ? static_cast<eng::u8>(eng::audio::pcm_codec::Codec::None) : acp_codec == 1u ? static_cast<eng::u8>(eng::audio::pcm_codec::Codec::DeltaRle) : acp_codec == 5u ? static_cast<eng::u8>(eng::audio::pcm_codec::Codec::FibDelta) : acp_codec == 6u ? static_cast<eng::u8>(eng::audio::pcm_codec::Codec::ImaAdpcm) : 0xffu;
				if (compression == 0xffu || unit.decoded_samples > scratch.size()) return -1;
				const eng::u8* pcm_source = m_blob.data() + payload_offset;
				if (acp_codec != 0u) { const eng::s32 decoded = eng::audio::pcm_codec::decode({m_blob.data() + payload_offset, payload_size}, {scratch.data(), unit.decoded_samples}, compression); if (decoded != static_cast<eng::s32>(unit.decoded_samples)) { std::fprintf(stderr, "ACP1 PCM decode fallo codec=%u payload=%u decoded=%ld esperado=%u\\n", acp_codec, payload_size, static_cast<long>(decoded), unit.decoded_samples); return -1; } pcm_source = scratch.data(); }
				const eng::u64 overlap_start = std::max<eng::u64>(event.start_sample, first_sample);
				const eng::u64 overlap_end = std::min(event_end, window_end);
				const eng::usize fade = std::min<eng::usize>(16u, event.duration / 2u);
				const double pitch_ratio = std::pow(2.0, static_cast<double>(event.pitch_semitones_q8_8) / (12.0 * 256.0));
				for (eng::u64 sample = overlap_start; sample < overlap_end; ++sample) {
					const eng::usize event_offset = static_cast<eng::usize>(sample - event.start_sample);
					double source_position = static_cast<double>(event.unit_offset) + event_offset * pitch_ratio;
					if ((unit.flags & 1u) != 0u) source_position = std::fmod(source_position, static_cast<double>(unit.decoded_samples));
					const eng::usize source = std::min<eng::usize>(unit.decoded_samples - 1u, static_cast<eng::usize>(source_position));
					const eng::usize next_source = std::min<eng::usize>(unit.decoded_samples - 1u, source + 1u);
					const double fraction = source_position - static_cast<double>(source);
					const double fade_in = fade == 0u ? 1.0 : std::min(1.0, static_cast<double>(event_offset + 1u) / fade);
					const eng::usize from_end = static_cast<eng::usize>(event_end - sample);
					const double fade_out = fade == 0u ? 1.0 : std::min(1.0, static_cast<double>(from_end) / fade);
					const double envelope = std::min(fade_in, fade_out);
					const double pcm_value = static_cast<double>(static_cast<eng::s8>(pcm_source[source])) * (1.0 - fraction) + static_cast<double>(static_cast<eng::s8>(pcm_source[next_source])) * fraction;
					const eng::s32 scaled = static_cast<eng::s32>(std::lround(pcm_value * envelope * event.gain_q8_8 * track.gain_q8_8 / 65536.0));
					accumulator[static_cast<eng::usize>(sample - first_sample)] += static_cast<eng::s16>(scaled);
				}
				continue;
			}
			eng::audio::acp1_v3::SynthesisParams params {};
			if (unit.synthesis_index == 0xffffu || !eng::audio::acp1_v3::synthesis(m_blob, m_v3_info, unit.synthesis_index, params)) return -1;
			const eng::u16 partial_count = std::min<eng::u16>(params.partial_count, kMaxPartials);
			eng::audio::SynthPartial partials[kMaxPartials] {};
			for (eng::u16 i = 0u; i < partial_count; ++i) {
				eng::audio::acp1_v3::Partial source {};
				if (!eng::audio::acp1_v3::partial(m_blob, m_v3_info, params.first_partial + i, source)) return -1;
				partials[i] = {source.ratio_q8_8, source.amplitude_q1_15, source.phase_q0_32};
			}
			const eng::u64 overlap_start = std::max<eng::u64>(event.start_sample, first_sample);
			const eng::u64 overlap_end = std::min(event_end, window_end);
			const eng::u32 event_offset = static_cast<eng::u32>(overlap_start - event.start_sample);
			const eng::usize output_offset = static_cast<eng::usize>(overlap_start - first_sample);
			const eng::usize render_count = static_cast<eng::usize>(overlap_end - overlap_start);
			eng::audio::SynthVoice voice {};
			eng::audio::apply_synth_event(voice, params.fundamental_hz_q16_16,
				{event.pitch_semitones_q8_8, static_cast<eng::u16>((static_cast<eng::u32>(event.gain_q8_8) * track.gain_q8_8) >> 8u)});
			voice.state.phase_q0_32 = params.phase_q0_32;
			eng::audio::advance_synth_voice(voice, m_v3_info.sample_rate, event_offset);
			if (!eng::audio::render_synth_voice(voice, {partials, partial_count}, m_v3_info.sample_rate, {scratch.data(), render_count})) return -1;
			for (eng::usize i = 0u; i < render_count; ++i) accumulator[output_offset + i] += static_cast<eng::s8>(scratch[i]);
		}
		for (eng::usize i = 0u; i < count; ++i) {
			const eng::s32 value = std::clamp<eng::s32>(accumulator[i], -128, 127);
			output[i] = value;
		}
		for (eng::usize i = count; i < output.size(); ++i) output[i] = 0u;
		return static_cast<eng::s32>(count);
	}

	static constexpr eng::u16 kMaxPartials = 64u; ///< Límite host de parciales materializados por ventana.
	eng::Span<const eng::u8> m_blob {}; ///< Vista no propietaria del ACP1 validado.
	eng::audio::media::Info m_info {}; ///< Metadatos y tablas validadas del contenedor.
	eng::audio::acp1_v3::Info m_v3_info {}; ///< Metadatos ACP1 v3 cuando la composición es aditiva.
	bool m_is_v3 = false; ///< Selecciona el decoder lineal v1/v2 o el renderer aditivo v3.
};

} // namespace audio_compressor::playback
