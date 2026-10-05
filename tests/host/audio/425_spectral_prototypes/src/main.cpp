// HOST-425: prototipos espectrales tiempo-frecuencia.

#include <cmath>
#include <cstdio>
#include <vector>

#include "../../../../../host-tools/audio-compressor/include/audio_compressor/dsp/spectral_prototype_separation.hpp"
#include "../../../../../host-tools/audio-compressor/src/acp1_v3_writer.hpp"

int main() {
	const eng::u32 rate = 11025u;
	std::vector<eng::u8> pcm(8192u, 128u);
	for (eng::usize i = 0u; i < pcm.size(); ++i) {
		const double t = static_cast<double>(i) / rate;
		const double value = 76.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * t) +
			42.0 * std::sin(2.0 * 3.141592653589793 * 660.0 * t);
		pcm[i] = static_cast<eng::u8>(static_cast<eng::s8>(std::clamp(static_cast<int>(std::lround(value)), -128, 127)));
	}
	audio_compressor::dsp::SpectralSeparationResult three {};
	audio_compressor::dsp::SpectralPrototypeOptions options {};
	options.max_prototypes = 3u;
	if (!audio_compressor::dsp::separate_spectral_prototypes(pcm, rate, options, three) || three.prototypes.empty() || three.prototypes.front().pcm.empty() || three.reconstructed_tracks.size() != three.prototypes.size() || three.estimated_bytes == 0u) {
		std::fprintf(stderr, "la separación espectral de tres prototipos falló\n"); return 1;
	}
	audio_compressor::dsp::SpectralSeparationResult eight {};
	options.max_prototypes = 8u;
	if (!audio_compressor::dsp::separate_spectral_prototypes(pcm, rate, options, eight) || eight.prototypes.size() < three.prototypes.size()) {
		std::fprintf(stderr, "la separación espectral de ocho prototipos falló\n"); return 1;
	}
	if (eight.metrics.residual_ratio > three.metrics.residual_ratio + 1.0e-9) {
		std::fprintf(stderr, "ocho prototipos no reducen el residual: %.6f > %.6f\n", eight.metrics.residual_ratio, three.metrics.residual_ratio); return 1;
	}
	audio_compressor::SpectralPcmTrack compact_track {};
	compact_track.pcm = three.prototypes.front().pcm;
	compact_track.events.push_back({0u, static_cast<eng::u32>(compact_track.pcm.size()), 256u, 0});
	std::vector<eng::u8> compact_file;
	if (!audio_compressor::build_acp1_v3_spectral({compact_track}, rate, compact_track.pcm.size(), compact_file) || compact_file.empty()) {
		std::fprintf(stderr, "el writer ACP1 espectral compacto falló\n"); return 1;
	}
	std::printf("OK: prototipos espectrales, 3=%zu 8=%zu residual3=%.6f residual8=%.6f\n", three.prototypes.size(), eight.prototypes.size(), three.metrics.residual_ratio, eight.metrics.residual_ratio);
	return 0;
}
