// HOST-421: reparto de voces de síntesis entre Paula, mixer y fallback OctaMED.

#include <cstdio>

#include <eng/audio/synth_voice_plan.hpp>

int main() {
	const eng::audio::SynthTrackRequest requests[7] {
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}},
	};
	eng::audio::SynthTrackAssignment assignments[7] {};
	if (eng::audio::plan_synth_tracks(requests, assignments, false) != eng::audio::SynthPlanStatus::Direct ||
		eng::audio::plan_synth_tracks(requests, assignments, true) != eng::audio::SynthPlanStatus::Direct) {
		std::fprintf(stderr, "planner no asignó las siete voces disponibles\n"); return 1;
	}
	const eng::audio::SynthTrackRequest overflow[8] {
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
	};
	eng::audio::SynthTrackAssignment overflow_assignments[8] {};
	if (eng::audio::plan_synth_tracks(overflow, overflow_assignments, false) != eng::audio::SynthPlanStatus::Rejected ||
		eng::audio::plan_synth_tracks(overflow, overflow_assignments, true) != eng::audio::SynthPlanStatus::NeedsOctaMED) {
		std::fprintf(stderr, "planner no declaró correctamente el fallback OctaMED\n"); return 1;
	}
	const eng::audio::SynthTrackRequest sequential[8] {
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {0u, 100u}}, {eng::audio::SynthRoute::Auto, {0u, 100u}},
		{eng::audio::SynthRoute::Auto, {100u, 200u}}, {eng::audio::SynthRoute::Auto, {100u, 200u}},
		{eng::audio::SynthRoute::Auto, {100u, 200u}}, {eng::audio::SynthRoute::Auto, {100u, 200u}},
	};
	eng::audio::SynthTrackAssignment sequential_assignments[8] {};
	if (eng::audio::plan_synth_tracks(sequential, sequential_assignments, false) != eng::audio::SynthPlanStatus::Direct) {
		std::fprintf(stderr, "planner no reutilizó voces en intervalos secuenciales\n"); return 1;
	}
	std::printf("OK: planner de tres voces Paula, cuatro mixer y fallback OctaMED.\n");
	return 0;
}
