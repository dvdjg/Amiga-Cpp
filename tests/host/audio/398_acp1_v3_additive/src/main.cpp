// HOST-398: writer ACP1 v3 aditivo y vistas de síntesis/eventos.

#include <cstdio>

#include <eng/audio/acp1_v3.hpp>
#include <eng/audio/synth_renderer.hpp>
#include "../../../../../host-tools/audio-compressor/src/acp1_v3_writer.hpp"

int main() {
	audio_compressor::AdditiveTrack track {};
	track.route = 3u; track.fundamental_hz_q16_16 = 440u << 16u;
	track.partials = {{256u, 24000, 0u}, {512u, 8000, 0u}};
	track.notes = {{0u, 128u, 0, 256u}, {128u, 128u, 3072, 192u}};
	std::vector<eng::u8> file;
	if (!audio_compressor::build_acp1_v3_additive({track}, 11025u, file)) {
		std::fprintf(stderr, "writer ACP1 v3 aditivo rechazó una pista válida\n"); return 1;
	}
	eng::audio::acp1_v3::Info info {};
	eng::audio::acp1_v3::Unit unit {};
	eng::audio::acp1_v3::SynthesisParams params {};
	eng::audio::acp1_v3::Partial partial {};
	eng::audio::acp1_v3::Event event {};
	const eng::Span<const eng::u8> view {file.data(), file.size()};
	if (!eng::audio::acp1_v3::parse(view, info) || info.unit_count != 1u || info.event_count != 2u ||
		!eng::audio::acp1_v3::unit(view, info, 0u, unit) || unit.representation != 1u ||
		!eng::audio::acp1_v3::synthesis(view, info, 0u, params) || params.partial_count != 2u ||
		!eng::audio::acp1_v3::partial(view, info, 1u, partial) || partial.ratio_q8_8 != 512u ||
		!eng::audio::acp1_v3::event(view, info, 1u, event) || event.pitch_semitones_q8_8 != 3072 || event.gain_q8_8 != 192u) {
		std::fprintf(stderr, "vistas ACP1 v3 no conservaron síntesis y eventos\n"); return 1;
	}
	std::printf("OK: ACP1 v3 aditivo, parciales y eventos de pitch/ganancia.\n");
	return 0;
}
