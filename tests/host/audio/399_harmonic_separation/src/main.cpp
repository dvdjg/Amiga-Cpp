// HOST-399: separador armónico host experimental.

#include <cmath>
#include <cstdio>
#include <vector>

#include "../../../../../host-tools/audio-compressor/include/audio_compressor/dsp/harmonic_separation.hpp"

int main() {
	const eng::u32 rate = 11025u;
	std::vector<eng::u8> pcm(4096u, 0u);
	for (eng::usize i = 0u; i < pcm.size(); ++i) {
		const double t = static_cast<double>(i) / rate;
		const double value = 75.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * t) +
			30.0 * std::sin(2.0 * 3.141592653589793 * 880.0 * t) +
			15.0 * std::sin(2.0 * 3.141592653589793 * 1320.0 * t);
		pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(static_cast<int>(std::lround(value)), -128, 127)));
	}
	audio_compressor::dsp::HarmonicSeparationOptions options {};
	options.max_tracks = 1u; options.min_hz = 400u; options.max_hz = 500u; options.frequency_step_hz = 2u; options.partials = 4u;
	std::vector<audio_compressor::dsp::HarmonicTrackModel> models;
	if (!audio_compressor::dsp::separate_harmonic(pcm, rate, options, models) || models.empty() || models.front().partials.empty()) {
		std::fprintf(stderr, "separador armónico no encontró ningún modelo\n"); return 1;
	}
	bool found_440 = false;
	for (const auto& model : models) found_440 = found_440 || ((model.fundamental_hz_q16_16 >> 16u) >= 430u && (model.fundamental_hz_q16_16 >> 16u) <= 455u);
	if (!found_440) { std::fprintf(stderr, "separador armónico no recuperó la fundamental de 440 Hz (modelos=%zu, primero=%u)\n", models.size(), models.empty() ? 0u : models.front().fundamental_hz_q16_16 >> 16u); return 1; }
	std::printf("OK: separación armónica experimental, modelos=%zu F0=%u.\n", models.size(), models.front().fundamental_hz_q16_16 >> 16u);
	return 0;
}
