#pragma once

/// Preparación de ventanas de síntesis para Paula directa o mixer sin escribir registros.

#include <eng/audio/audio_mode.hpp>
#include <eng/audio/synth_renderer.hpp>
#include <eng/audio/synth_voice_plan.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::audio {

/// Metadatos que el backend materializa después de preparar una ventana PCM8.
struct SynthPlaybackWindow {
	SynthRoute route = SynthRoute::Auto; ///< Ruta lógica elegida por el planner.
	eng::u8 hardware_channel = 0u; ///< AUD1..AUD3 para Paula o AUD0 para mixer.
	eng::u16 period = 0u; ///< Periodo Paula calculado para la fundamental actual.
	eng::u8 volume = 0u; ///< Volumen Paula 0..64; el mixer usa su propia escala.
	eng::u16 sample_count = 0u; ///< Muestras válidas de la ventana preparada.
};

/// Prepara PCM8 y metadatos de una ventana; la IRQ solo recibirá el buffer ya listo.
[[nodiscard]] inline bool prepare_synth_window(SynthVoice& voice, eng::Span<const SynthPartial> partials,
	eng::u32 sample_rate, const SynthTrackAssignment& assignment, eng::Span<eng::u8> output,
	SynthPlaybackWindow& result) noexcept {
	if (assignment.route != SynthRoute::PaulaRequired && assignment.route != SynthRoute::MixerRequired) return false;
	if (output.empty() || output.size() > 65535u || !render_synth_voice(voice, partials, sample_rate, output)) return false;
	result.route = assignment.route;
	result.hardware_channel = assignment.route == SynthRoute::PaulaRequired ? assignment.voice + 1u : 0u;
	const eng::u32 hz = voice.fundamental_hz_q16_16 >> 16u;
	result.period = paula::period_for_hz(hz);
	const eng::u32 scaled_volume = (voice.level_q8_8 * 64u) / 256u;
	result.volume = scaled_volume > 64u ? 64u : scaled_volume;
	result.sample_count = output.size();
	if (assignment.route == SynthRoute::MixerRequired) {
		for (eng::u8& sample : output) {
			const eng::s32 signed_sample = sample;
			const eng::s32 scaled_sample = signed_sample >= 128 ? (signed_sample - 256) / 4 : signed_sample / 4;
			sample = scaled_sample;
		}
	}
	return true;
}

} // namespace eng::audio
