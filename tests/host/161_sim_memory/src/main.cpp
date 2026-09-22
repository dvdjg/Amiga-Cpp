// ============================================================================
// Test HOST-161: memoria de corto y largo plazo (integracion y consolidacion)
// ============================================================================
//
// Valida `eng/sim/memory.hpp`:
//
//   1) `integrate_observations`: la percepcion alimenta el corto plazo (trackers),
//      acumulando las modalidades y conservando/realzando la saliencia.
//   2) `working_strength`: fuerza en corto plazo de un objetivo.
//   3) `forget_working`: olvido rapido del corto plazo.
//   4) `consolidate`: lo fuerte/saliente pasa a largo plazo (`KnowledgeSet`), con el mapeo
//      de categorias y el sujeto correcto (entidad o region).
//   5) `recall`/`recalls_shelter`/`recalls_danger`: lectura del largo plazo.
//   6) Integracion en `SimWorld`: percibir -> integrar -> consolidar.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/161_sim_memory

#include <cstdio>

#include <eng/sim/memory.hpp>
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

void test_integrate() {
	TrackerList<6> tr;
	Observation o1 {};
	o1.target = 5u;
	o1.kind = TrackerKind::Threat;
	o1.room = 0u;
	o1.x = 3;
	o1.y = 0;
	o1.strength = 200u;
	o1.salience = 200u;
	o1.modalities = static_cast<eng::u8>(sense_bit::sight | sense_bit::hearing);
	integrate_observations(tr, eng::Span<const Observation> {&o1, 1}, 0u);

	auto t = find_tracker(tr, 5u, TrackerKind::Threat);
	check(t.valid() && t->confidence == 200u &&
		      t->modalities == static_cast<eng::u8>(sense_bit::sight | sense_bit::hearing),
	      "memoria: la observacion entra en el corto plazo");

	// Una segunda modalidad (mas debil) se une y la recurrencia sube la saliencia.
	Observation o2 {};
	o2.target = 5u;
	o2.kind = TrackerKind::Threat;
	o2.room = 0u;
	o2.x = 3;
	o2.y = 0;
	o2.strength = 150u;
	o2.salience = 150u;
	o2.modalities = sense_bit::smell;
	integrate_observations(tr, eng::Span<const Observation> {&o2, 1}, 1u);
	t = find_tracker(tr, 5u, TrackerKind::Threat);
	check(t->confidence == 200u &&
		      t->modalities == static_cast<eng::u8>(sense_bit::sight | sense_bit::hearing |
							    sense_bit::smell),
	      "memoria: se unen las modalidades sin perder confianza");
	check(t->salience == 208u, "memoria: la recurrencia realza la saliencia");
	check(working_strength(tr, 5u) == 200u, "memoria: fuerza en corto plazo");
}

void test_forget_consolidate() {
	TrackerList<6> tr;
	Observation foe {};
	foe.target = 5u;
	foe.kind = TrackerKind::Threat;
	foe.room = 0u;
	foe.x = 3;
	foe.strength = 200u;
	foe.salience = 200u;
	Observation den {};
	den.target = 7u; // entidad-refugio
	den.kind = TrackerKind::Den;
	den.room = 3u;
	den.x = 9;
	den.strength = 200u;
	den.salience = 200u;
	const Observation both[2] = {foe, den};
	integrate_observations(tr, eng::Span<const Observation> {both, 2}, 0u);

	MemoryParams mp {};
	mp.forget_rate = 40u;
	forget_working(tr, mp);
	check(find_tracker(tr, 5u, TrackerKind::Threat)->confidence == 160u,
	      "memoria: el corto plazo se olvida");

	mp.consolidation_threshold = 100u;
	KnowledgeSet kn;
	const eng::u8 n = consolidate(tr, kn, mp);
	check(n == 2u, "memoria: se consolidan las creencias sostenidas");
	check(recall(kn, KnowledgeKind::Enemy, 5u) == 128u,
	      "memoria: un peligro se recuerda como enemigo del individuo");
	check(recalls_shelter(kn, 3u),
	      "memoria: un refugio se recuerda como lugar (sujeto = region)");
	check(knowledge_of(TrackerKind::Threat) == KnowledgeKind::Enemy &&
		      knowledge_is_place(TrackerKind::Den),
	      "memoria: mapeo de categorias y lugares");

	// Lo que no alcanza el umbral no se consolida.
	TrackerList<6> weak;
	Observation faint {};
	faint.target = 9u;
	faint.kind = TrackerKind::Noise;
	faint.room = 1u;
	faint.strength = 40u;
	faint.salience = 40u;
	integrate_observations(weak, eng::Span<const Observation> {&faint, 1}, 0u);
	KnowledgeSet kn2;
	check(consolidate(weak, kn2, mp) == 0u, "memoria: lo debil no se consolida");
}

void test_world() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	MemoryParams mp {};
	mp.consolidation_threshold = 80u;
	w.set_memory_params(mp);

	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId b = w.spawn(2u, 0u, 0u, 5, 0);
	w.find(a)->set_realized(true);
	w.find(b)->set_realized(true);

	SenseTarget st {};
	st.id = b;
	st.room = 0u;
	st.x = 5;
	st.y = 0;
	st.size = 50u;
	st.sound = 180u;
	st.visible = true;
	st.kind = TrackerKind::Prey;

	Observation obs[4];
	const eng::u8 n = w.sense(a, eng::Span<const SenseTarget> {&st, 1},
				  eng::Span<Observation> {obs, 4});
	check(n == 1u, "mundo: percibe a la presa");
	w.integrate_senses(a, eng::Span<const Observation> {obs, n});
	check(working_strength(w.find(a)->trackers, b) > 0u,
	      "mundo: la presa entra en el corto plazo");
	check(w.consolidate_memory(a) > 0u && recall(w.find(a)->knowledge, KnowledgeKind::Prey, b) > 0u,
	      "mundo: la presa pasa a largo plazo");
	(void)w.tick_memory();
}

} // namespace

int main() {
	std::printf("Sim memory:\n");
	test_integrate();
	test_forget_consolidate();
	test_world();

	if (g_fail == 0u) {
		std::printf("OK: Sim memory (corto plazo, olvido, consolidacion, largo plazo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
