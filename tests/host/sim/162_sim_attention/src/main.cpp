// ============================================================================
// Test HOST-162: atencion (percepcion<->conducta) y sentidos por genetica
// ============================================================================
//
// Valida:
//   - `eng/sim/senses.hpp`: `focused` (el miedo estrecha/acorta la vista y agudiza el
//     oido; la ira enfoca), `senses_from_genome` y la ecolocalizacion (el oido cruza
//     regiones casi sin penalizacion).
//   - `eng/sim/behavior.hpp`: la atencion elige el tracker por confianza + saliencia +
//     multimodalidad (`attention_score`/`best_attention_tracker`), de modo que lo que se
//     percibe por varios sentidos pesa mas.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/162_sim_attention

#include <cstdio>

#include <eng/sim/behavior.hpp>
#include <eng/sim/genetics.hpp>
#include <eng/sim/senses.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_focus() {
	Senses base {};
	base.vision_arc = 120u;
	base.vision_range = 20u;
	base.smell_range = 10u;
	base.hearing_range = 12u;

	const Senses calm = focused(base, 0u, 0u);
	check(calm.vision_arc == base.vision_arc && calm.vision_range == base.vision_range,
	      "atencion: sin miedo los sentidos no cambian");

	const Senses scared = focused(base, 255u, 0u);
	check(scared.vision_arc < base.vision_arc &&
		      scared.vision_arc >= AttentionParams {}.min_arc,
	      "atencion: el miedo estrecha el cono (con suelo)");
	check(scared.vision_range < base.vision_range, "atencion: el miedo acorta la vista");
	check(scared.hearing_range > base.hearing_range,
	      "atencion: el miedo agudiza el oido");
	check(scared.smell_range < base.smell_range, "atencion: el miedo reduce el olfato");

	const Senses furious = focused(base, 0u, 255u);
	check(furious.acuity > base.acuity, "atencion: la ira enfoca");
}

void test_genome_senses() {
	Genome slow {};
	slow.set(Gene::Speed, 20u);
	Genome fast {};
	fast.set(Gene::Speed, 90u);
	const Senses fs = senses_from_genome(fast);
	const Senses ss = senses_from_genome(slow);
	check(fs.vision > ss.vision && fs.vision_range >= ss.vision_range,
	      "genoma: mas velocidad -> mejor vista");
	check(fs.echolocation > 0u && ss.echolocation == 0u,
	      "genoma: la velocidad alta desarrolla ecolocalizacion");

	// La ecolocalizacion oye entre regiones casi como si las viera.
	SenseTarget other_room {};
	other_room.id = 3u;
	other_room.room = 1u;
	other_room.sound = 200u;
	SenseParams sp {};
	Senses blind {};
	blind.hearing = 80u;
	Senses echo = blind;
	echo.echolocation = 90u;
	check(hearing_strength(echo, false, other_room, 0u, sp) >
		      hearing_strength(blind, false, other_room, 0u, sp),
	      "genoma: la ecolocalizacion mejora el oido entre regiones");
}

void test_attention() {
	TrackerList<4> tr;
	// Deteccion fuerte pero de un solo sentido.
	observe(tr, TrackerKind::Threat, 1u, 0u, 0, 0, 150u, 0u);
	// Deteccion algo mas debil pero multimodal y saliente.
	observe(tr, TrackerKind::Threat, 2u, 0u, 0, 0, 120u, 0u);
	auto multi = find_tracker(tr, 2u, TrackerKind::Threat);
	multi->modalities = static_cast<eng::u8>(sense_bit::sight | sense_bit::hearing |
						 sense_bit::smell);
	multi->salience = 100u;

	auto best = best_attention_tracker(tr, TrackerKind::Threat);
	check(best.valid() && best->target == 2u,
	      "atencion: lo multimodal+saliente gana a una deteccion fuerte monomodal");
	check(attention_score(*multi) > attention_score(*find_tracker(tr, 1u, TrackerKind::Threat)),
	      "atencion: la puntuacion combina confianza, saliencia y modalidades");
}

} // namespace

int main() {
	std::printf("Sim attention:\n");
	test_focus();
	test_genome_senses();
	test_attention();

	if (g_fail == 0u) {
		std::printf("OK: Sim attention (foco, sentidos por genoma, atencion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
