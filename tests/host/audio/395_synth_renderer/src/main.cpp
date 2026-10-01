// HOST-395: renderer entero de voz aditiva por ventanas.

#include <cstdio>
#include <cstring>

#include <eng/audio/synth_renderer.hpp>

int main() {
	const eng::audio::SynthPartial partials[] {
		{256u, 24000, 0u},
		{512u, 12000, 0u},
		{768u, 6000, 0u},
	};
	eng::audio::SynthVoice voice {};
	voice.fundamental_hz_q16_16 = 440u << 16u;
	eng::u8 first[128] {}, second[128] {};
	if (!eng::audio::render_synth_voice(voice, partials, 11025u, first) ||
		!eng::audio::render_synth_voice(voice, partials, 11025u, second) ||
		std::memcmp(first, second, sizeof(first)) == 0) {
		std::fprintf(stderr, "renderer aditivo no conserva una fase continua\n"); return 1;
	}
	eng::audio::apply_synth_event(voice, 440u << 16u, {3072, 192u});
	if (voice.fundamental_hz_q16_16 < (879u << 16u) || voice.fundamental_hz_q16_16 > (881u << 16u) || voice.level_q8_8 != 192u) {
		std::fprintf(stderr, "renderer aditivo no aplicó pitch/ganancia del evento\n"); return 1;
	}
	eng::audio::SynthVoice silent {};
	if (eng::audio::render_synth_voice(silent, partials, 11025u, first)) {
		std::fprintf(stderr, "renderer aditivo aceptó una frecuencia nula\n"); return 1;
	}
	std::printf("OK: renderer aditivo PCM8, parciales y fase entre ventanas.\n");
	return 0;
}
