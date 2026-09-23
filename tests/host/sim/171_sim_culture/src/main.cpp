// ============================================================================
// Test HOST-171: cultura y rituales (tradiciones heredadas por ensenanza)
// ============================================================================
//
// Valida `eng/sim/culture.hpp` y `SimWorld::enact_ritual`:
//
//   1) Aprender/consultar rituales (`RitualKind` como conocimiento con sujeto propio).
//   2) `signal_for_ritual`: como se expresa cada ritual.
//   3) `perform_ritual`: efecto emocional (luto consuela, festejo alegra, caza enfurece).
//   4) `ritual_for_event`: que ritual se dispara ante un evento segun lo aprendido.
//   5) Transmision por ensenanza (`share`) y actuacion en el mundo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/171_sim_culture

#include <cstdio>

#include <eng/sim/culture.hpp>
#include <eng/sim/world.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_knowledge_and_signals() {
	KnowledgeSet kn;
	check(!knows_ritual(kn, RitualKind::Greeting), "cultura: arranca sin rituales");
	learn_ritual(kn, RitualKind::Greeting, 200u);
	check(knows_ritual(kn, RitualKind::Greeting) &&
		      ritual_confidence(kn, RitualKind::Greeting) == 200u,
	      "cultura: se aprende un ritual");
	check(signal_for_ritual(RitualKind::Greeting) == SignalKind::Greet &&
		      signal_for_ritual(RitualKind::HuntCall) == SignalKind::Threat,
	      "cultura: cada ritual tiene su gesto");
}

void test_perform() {
	Mind sad {};
	sad.emotions.sadness = 100u;
	perform_ritual(sad, RitualKind::Mourning);
	check(sad.emotions.sadness < 100u && sad.emotions.cordiality > 128u,
	      "cultura: el luto consuela y acerca");

	Mind m {};
	perform_ritual(m, RitualKind::Feast);
	check(m.emotions.joy > 128u, "cultura: el festejo alegra");
	perform_ritual(m, RitualKind::HuntCall);
	check(m.emotions.anger > 0u, "cultura: la llamada de caza enfurece");
}

void test_event_trigger() {
	KnowledgeSet kn;
	learn_ritual(kn, RitualKind::Burial, 150u);
	learn_ritual(kn, RitualKind::Mourning, 200u);
	check(ritual_for_event(kn, CultureEvent::AllyDied) == RitualKind::Mourning,
	      "cultura: ante una muerte se dispara el ritual mas arraigado");
	check(ritual_for_event(kn, CultureEvent::FoodFound) == RitualKind::Count,
	      "cultura: sin fiesta aprendida no hay ritual de comida");
	learn_ritual(kn, RitualKind::Feast, 180u);
	check(ritual_for_event(kn, CultureEvent::FoodFound) == RitualKind::Feast,
	      "cultura: con fiesta aprendida se dispara");
}

void test_spread_and_world() {
	// Transmision por ensenanza: el ritual viaja con el conocimiento.
	KnowledgeSet teacher;
	KnowledgeSet student;
	learn_ritual(teacher, RitualKind::Mourning, 220u);
	check(share(teacher, student) >= 1u && knows_ritual(student, RitualKind::Mourning),
	      "cultura: el ritual se hereda al ensenar");

	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId b = w.spawn(1u, 0u, 0u, 2, 0);
	w.find(a)->set_realized(true);
	w.find(b)->set_realized(true);
	learn_ritual(w.find(a)->knowledge, RitualKind::Greeting, 200u);
	const eng::u8 heard = w.enact_ritual(a, RitualKind::Greeting);
	check(heard == 1u && w.find(b)->mind.emotions.cordiality > 128u &&
		      confidence_of(w.find(b)->trackers, a, TrackerKind::Friend) > 0u,
	      "cultura: actuar el ritual se percibe en la region");
}

} // namespace

int main() {
	std::printf("Sim culture:\n");
	test_knowledge_and_signals();
	test_perform();
	test_event_trigger();
	test_spread_and_world();

	if (g_fail == 0u) {
		std::printf("OK: Sim culture (rituales, efecto, disparo, ensenanza, mundo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
