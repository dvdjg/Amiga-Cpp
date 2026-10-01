#pragma once

/// Player host de ACP1 v1/v2 y ACP1 v3 aditivo sobre ventanas, sin ownership del blob ni buffers.

#include <algorithm>

#include <eng/audio/acp1_v3.hpp>
#include <eng/audio/media.hpp>
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
		return read_v3_window(first_sample, output, scratch, accumulator);
	}

private:
	/// Renderiza eventos aditivos ACP1 v3 en un acumulador host; el mismo algoritmo se reutiliza como referencia del player Amiga.
	[[nodiscard]] eng::s32 read_v3_window(eng::u32 first_sample, eng::Span<eng::u8> output,
		eng::Span<eng::u8> scratch, eng::Span<eng::s16> accumulator) const noexcept {
		if (output.empty() || scratch.size() < output.size() || accumulator.size() < output.size() ||
			static_cast<eng::u64>(first_sample) >= m_v3_info.timeline_samples) return -1;
		const eng::usize count = std::min<eng::u64>(output.size(), m_v3_info.timeline_samples - first_sample);
		for (eng::usize i = 0u; i < count; ++i) accumulator[i] = 0;
		for (eng::u32 event_index = 0u; event_index < m_v3_info.event_count; ++event_index) {
			eng::audio::acp1_v3::Event event {};
			if (!eng::audio::acp1_v3::event(m_blob, m_v3_info, event_index, event)) return -1;
			const eng::u64 event_end = event.start_sample + event.duration;
			const eng::u64 window_end = static_cast<eng::u64>(first_sample) + count;
			if (event_end <= first_sample || event.start_sample >= window_end) continue;
			eng::audio::acp1_v3::Unit unit {};
			eng::audio::acp1_v3::SynthesisParams params {};
			eng::audio::acp1_v3::Track track {};
			if (!eng::audio::acp1_v3::unit(m_blob, m_v3_info, event.unit_id, unit) || unit.synthesis_index == 0xffffu ||
				!eng::audio::acp1_v3::synthesis(m_blob, m_v3_info, unit.synthesis_index, params) ||
				!eng::audio::acp1_v3::track(m_blob, m_v3_info, event.track_id, track)) return -1;
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
