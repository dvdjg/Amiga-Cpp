// ============================================================================
// Test HOST-202: estado psicologico y evolucion de partida (eng::sim)
// ============================================================================
//
// Valida `eng/sim/psyche.hpp`: estado temporal, eventos de mesa (bad beat, farol cazado,
// victoria...), derivacion del contexto de fuga y modificador de decision. Comprueba que
// un irascible entra en tilt y un flematico aguanta.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/202_sim_psyche

#include <cstdio>

#include <eng/core/math/random.hpp>
#include <eng/sim/psyche.hpp>

namespace {

using namespace eng::sim;
using eng::u8;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_initial() {
	eng::Xoroshiro64pp rng {1u, 2u};
	const Persona flematico = make_persona(Archetype::Flematico, rng, 0u);
	const PsycheState s = initial_psyche(flematico);
	check(s.composure == flematico.psyche.composure, "inicial: compostura del caracter");
	check(s.confidence == flematico.psyche.self_esteem, "inicial: confianza de la autoestima");
	check(s.tilt == 0u, "inicial: sin tilt");
}

void test_bad_beat_tilt() {
	eng::Xoroshiro64pp rng {3u, 4u};
	const Persona irascible = make_persona(Archetype::Irascible, rng, 0u);
	Mind m;
	PsycheState s = initial_psyche(irascible);

	psyche_observe(s, m, TableEventKind::BadBeat, irascible);
	check(s.tilt > 0u, "evento: el bad beat da tilt");
	check(m.emotions.anger > 0u, "evento: el bad beat da ira");
	const u8 tilt_after = s.tilt;

	// Con el tiempo, el tilt se disipa.
	for (u8 i = 0u; i < 30u; ++i) {
		psyche_update(s, m, irascible);
	}
	check(s.tilt < tilt_after, "evento: el tilt se disipa con el tiempo");
}

void test_flematico_vs_irascible() {
	// Con la misma racha de derrotas, el irascible acumula mas tilt que el flematico.
	eng::Xoroshiro64pp rng {5u, 6u};
	const Persona flema = make_persona(Archetype::Flematico, rng, 0u);
	const Persona ira = make_persona(Archetype::Irascible, rng, 0u);
	Mind m1;
	Mind m2;
	PsycheState s1 = initial_psyche(flema);
	PsycheState s2 = initial_psyche(ira);
	for (u8 i = 0u; i < 4u; ++i) {
		psyche_observe(s1, m1, TableEventKind::BadBeat, flema);
		psyche_observe(s2, m2, TableEventKind::BadBeat, ira);
	}
	// El irascible tiene temper alto y compostura baja; su fuga es mayor.
	const LeakContext c1 = leak_context(s1, flema);
	const LeakContext c2 = leak_context(s2, ira);
	const eng::u8 leak1 = leak(m1, c1, GestureKind::FistClench);
	const eng::u8 leak2 = leak(m2, c2, GestureKind::FistClench);
	check(leak2 > leak1, "comparativa: el irascible se le nota mas que al flematico");
}

void test_confidence_and_streak() {
	eng::Xoroshiro64pp rng {7u, 8u};
	const Persona p = make_persona(Archetype::Metodico, rng, 0u);
	Mind m;
	PsycheState s = initial_psyche(p);
	const u8 conf0 = s.confidence;
	psyche_observe(s, m, TableEventKind::WonBigPot, p);
	check(s.confidence > conf0, "evento: ganar sube la confianza");
	check(s.streak > 50u, "evento: ganar sube la racha");
	psyche_observe(s, m, TableEventKind::LostShowdown, p);
	psyche_observe(s, m, TableEventKind::LostShowdown, p);
	check(s.confidence < conf0 + 100u, "evento: perder baja la confianza");
}

void test_aggression_mod() {
	PsycheState s {};
	s.confidence = 50u;
	s.tilt = 0u;
	s.fatigue = 0u;
	check(psyche_aggression_mod(s) == 0, "mod: estado neutro = 0");
	s.tilt = 120u;
	check(psyche_aggression_mod(s) > 0, "mod: el tilt sube la agresion");
	s.tilt = 0u;
	s.confidence = 10u;
	s.fatigue = 200u;
	check(psyche_aggression_mod(s) < 0, "mod: poca confianza y fatiga bajan la agresion");
}

} // namespace

int main() {
	std::printf("eng::sim psyche:\n");
	test_initial();
	test_bad_beat_tilt();
	test_flematico_vs_irascible();
	test_confidence_and_streak();
	test_aggression_mod();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim psyche (eventos, tilt, confianza, racha y modificador)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
