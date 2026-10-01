#pragma once

/// Renderer entero de una voz armónica ACP1: produce PCM8 por ventanas y conserva la fase entre ellas.

#include <eng/audio/wave_tables.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Forma de onda base usada por una voz aditiva cuando no se referencia una wavetable custom.
enum class SynthWaveform : eng::u8 { Sine = 0u, Triangle = 2u, Square = 3u };

/// Parcial armónico de una voz; la relación está en Q8.8 y la amplitud en Q1.15.
struct SynthPartial {
	eng::u16 ratio_q8_8 = 256u; ///< Relación respecto a la fundamental; 256 = 1x.
	eng::s16 amplitude_q1_15 = 0; ///< Amplitud firmada; 32767 = unidad.
	eng::u32 phase_q0_32 = 0u; ///< Desfase inicial del parcial en un ciclo completo.
};

/// Estado continuo de una voz entre buffers; no reserva memoria ni conoce Paula.
struct SynthVoiceState {
	eng::u32 phase_q0_32 = 0u; ///< Fase de la fundamental al comienzo de la siguiente muestra.
};

/// Parámetros de una voz aditiva que el planificador puede actualizar por evento.
struct SynthVoice {
	eng::u32 fundamental_hz_q16_16 = 0u; ///< Frecuencia fundamental en Hz Q16.16.
	eng::u16 level_q8_8 = 256u; ///< Ganancia de la suma; 256 = unidad.
	SynthWaveform waveform = SynthWaveform::Sine; ///< Forma base de cada parcial.
	SynthVoiceState state {}; ///< Fase persistente entre ventanas consecutivas.
};

/// Controles de un evento ACP1 aplicados antes de renderizar su siguiente ventana.
struct SynthEventControls {
	eng::s16 pitch_semitones_q8_8 = 0; ///< Desplazamiento de pitch; 256 = un semitono.
	eng::u16 gain_q8_8 = 256u; ///< Ganancia del evento; 256 = unidad.
};

/// Convierte una frecuencia base y un pitch ACP1 a Hz Q16.16 sin usar coma flotante.
[[nodiscard]] inline eng::u32 synth_frequency_for_pitch(eng::u32 base_hz_q16_16, eng::s16 pitch_q8_8) noexcept {
	static constexpr eng::u32 kSemitoneRatioQ16_16[12] = {
		65536u, 69433u, 73513u, 77936u, 82570u, 87456u, 92682u,
		98193u, 104038u, 110219u, 116772u, 123716u,
	};
	eng::s32 semitones = pitch_q8_8 >= 0 ? (pitch_q8_8 + 128) / 256 : (pitch_q8_8 - 128) / 256;
	eng::s32 octave = 0;
	while (semitones >= 12) { semitones -= 12; ++octave; }
	while (semitones < 0) { semitones += 12; --octave; }
	eng::u64 frequency = eng::u64 {base_hz_q16_16} * kSemitoneRatioQ16_16[semitones] >> 16u;
	if (octave > 0) frequency <<= octave;
	if (octave < 0) frequency >>= -octave;
	return frequency > 0xffffffffu ? 0xffffffffu : frequency;
}

/// Aplica pitch y ganancia de un evento a una voz antes de llamar al renderer de ventana.
inline void apply_synth_event(SynthVoice& voice, eng::u32 base_hz_q16_16, SynthEventControls controls) noexcept {
	voice.fundamental_hz_q16_16 = synth_frequency_for_pitch(base_hz_q16_16, controls.pitch_semitones_q8_8);
	voice.level_q8_8 = controls.gain_q8_8;
}

/// Lee una tabla estándar y devuelve una muestra signed de amplitud aproximada ±127.
[[nodiscard]] constexpr eng::s16 synth_wave_sample(SynthWaveform waveform, eng::u32 phase) noexcept {
	const eng::u32 index = phase >> 26u;
	switch (waveform) {
		case SynthWaveform::Triangle: return triangle_byte(index);
		case SynthWaveform::Square: return square_byte(index);
		case SynthWaveform::Sine: default: return sine_byte(index);
	}
}

/// Renderiza una ventana PCM8 y conserva la fase para la siguiente llamada.
[[nodiscard]] inline bool render_synth_voice(SynthVoice& voice, eng::Span<const SynthPartial> partials,
	eng::u32 sample_rate, eng::Span<eng::u8> output) noexcept {
	if (sample_rate == 0u || voice.fundamental_hz_q16_16 == 0u || partials.empty() || output.empty()) return false;
	const auto phase_step = (eng::u64 {voice.fundamental_hz_q16_16} << 16u) / sample_rate;
	for (eng::usize sample = 0u; sample < output.size(); ++sample) {
		eng::s32 mixed = 0;
		for (const SynthPartial& partial : partials) {
			const auto partial_phase = (eng::u64 {voice.state.phase_q0_32} * partial.ratio_q8_8) >> 8u;
			const eng::s32 contribution = synth_wave_sample(voice.waveform, partial_phase + partial.phase_q0_32) * partial.amplitude_q1_15;
			mixed += contribution >> 15u;
		}
		mixed = (mixed * voice.level_q8_8) >> 8u;
		if (mixed < -128) mixed = -128;
		if (mixed > 127) mixed = 127;
		output[sample] = mixed;
		voice.state.phase_q0_32 += phase_step;
	}
	return true;
}

} // namespace eng::audio
