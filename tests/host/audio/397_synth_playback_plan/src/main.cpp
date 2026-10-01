// HOST-397: preparación de ventanas de síntesis para Paula y mixer.

#include <cstdio>

#include <eng/audio/synth_playback_plan.hpp>

int main() {
	const eng::audio::SynthPartial partials[] {{256u, 32767, 0u}};
	eng::audio::SynthVoice voice {};
	eng::audio::apply_synth_event(voice, 440u << 16u, {0, 256u});
	const eng::audio::SynthTrackAssignment paula {eng::audio::SynthRoute::PaulaRequired, 1u};
	eng::audio::SynthPlaybackWindow prepared {};
	eng::u8 paula_buffer[64] {};
	if (!eng::audio::prepare_synth_window(voice, partials, 11025u, paula, paula_buffer, prepared) ||
		prepared.hardware_channel != 2u || prepared.period != eng::audio::paula::period_for_hz(440u)) {
		std::fprintf(stderr, "preparación Paula inválida\n"); return 1;
	}
	const eng::audio::SynthTrackAssignment mixer {eng::audio::SynthRoute::MixerRequired, 0u};
	eng::u8 mixer_buffer[64] {};
	if (!eng::audio::prepare_synth_window(voice, partials, 11025u, mixer, mixer_buffer, prepared) ||
		prepared.hardware_channel != 0u || static_cast<eng::s8>(mixer_buffer[1]) > 31 || static_cast<eng::s8>(mixer_buffer[1]) < -32) {
		std::fprintf(stderr, "preparación mixer inválida\n"); return 1;
	}
	std::printf("OK: ventanas de síntesis preparadas para Paula y mixer.\n");
	return 0;
}
