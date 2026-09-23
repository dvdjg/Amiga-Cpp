// ============================================================================
// Test HOST-170: lenguaje y gestos (comunicacion atada a emocion y jerarquia)
// ============================================================================
//
// Valida `eng/sim/communication.hpp` y `SimWorld::broadcast_signals`:
//
//   1) `signal_for_behavior`/`signal_intensity`: la conducta y la emocion eligen la senal.
//   2) `make_signal`: el alcance depende del oido y de la ecolocalizacion.
//   3) `receive_signals`: quien esta en alcance registra el tracker correspondiente.
//   4) `apply_signal_effect`: el contenido emocional (alarma -> miedo; sumision -> eleva
//      al receptor y lo sosiega).
//   5) `broadcast_signals`: entrega entre criaturas de la misma region.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/170_sim_communication

#include <cstdio>

#include <eng/sim/communication.hpp>
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

void test_mapping() {
	Mind fear {};
	fear.emotions.fear = 200u;
	check(signal_for_behavior(Behavior::Flee, fear) == SignalKind::Alarm &&
		      signal_for_behavior(Behavior::Court, Mind {}) == SignalKind::Mating &&
		      signal_for_behavior(Behavior::Submit, Mind {}) == SignalKind::Submit &&
		      signal_for_behavior(Behavior::SeekFood, Mind {}) == SignalKind::Food,
	      "lenguaje: la conducta elige el gesto");
	check(signal_intensity(Behavior::Flee, fear) > signal_intensity(Behavior::Idle, Mind {}),
	      "lenguaje: la urgencia sube la intensidad");

	Senses plain {};
	Senses echo {};
	echo.hearing = 90u;
	echo.echolocation = 90u;
	const Signal s1 = make_signal(1u, 0u, 0u, 0, 0, Behavior::Flee, fear, plain);
	const Signal s2 = make_signal(1u, 0u, 0u, 0, 0, Behavior::Flee, fear, echo);
	check(s2.range > s1.range, "lenguaje: la ecolocalizacion amplia el alcance");
}

void test_receive() {
	const Signal s {5u, 0u, 0u, 0, 0, SignalKind::Alarm, 200u, 10u};
	TrackerList<4> near;
	const eng::u8 h1 = receive_signals(near, eng::Span<const Signal> {&s, 1}, 0u, 5, 0, 0u);
	check(h1 == 1u && confidence_of(near, 5u, TrackerKind::Threat) > 0u,
	      "lenguaje: la alarma se registra como amenaza a quien la oye");

	TrackerList<4> far;
	const eng::u8 h2 = receive_signals(far, eng::Span<const Signal> {&s, 1}, 0u, 50, 0, 0u);
	check(h2 == 0u, "lenguaje: fuera de alcance no se oye");

	TrackerList<4> other_room;
	const eng::u8 h3 = receive_signals(other_room, eng::Span<const Signal> {&s, 1}, 1u, 0, 0, 0u);
	check(h3 == 0u, "lenguaje: no cruza de region (de momento)");
}

void test_effect() {
	Mind m {};
	apply_signal_effect(m, SignalKind::Alarm);
	check(m.emotions.fear > 0u, "lenguaje: la alarma da miedo");
	apply_signal_effect(m, SignalKind::Greet);
	check(m.emotions.cordiality > 128u, "lenguaje: el saludo acerca");

	Mind sub {};
	sub.deference = 100u;
	apply_signal_effect(sub, SignalKind::Submit);
	check(sub.deference < 100u && sub.emotions.joy > 128u,
	      "lenguaje: recibir sumision eleva y sosiega");
}

void test_broadcast() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId b = w.spawn(1u, 0u, 0u, 2, 0);
	const EntityId c = w.spawn(1u, 0u, 1u, 0, 0); // otra region
	w.find(a)->set_realized(true);
	w.find(b)->set_realized(true);
	w.find(c)->set_realized(true);
	w.find(a)->behavior = Behavior::Flee;
	w.find(a)->mind.emotions.fear = 220u;

	const eng::u8 heard = w.broadcast_signals();
	check(heard >= 1u, "lenguaje: hay senales entregadas");
	check(confidence_of(w.find(b)->trackers, a, TrackerKind::Threat) > 0u &&
		      w.find(b)->mind.emotions.fear > 0u,
	      "lenguaje: el vecino registra la amenaza y se asusta");
	check(confidence_of(w.find(c)->trackers, a, TrackerKind::Threat) == 0u,
	      "lenguaje: la otra region no recibe la senal");
}

} // namespace

int main() {
	std::printf("Sim communication:\n");
	test_mapping();
	test_receive();
	test_effect();
	test_broadcast();

	if (g_fail == 0u) {
		std::printf("OK: Sim communication (gestos, recepcion, emocion, difusion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
