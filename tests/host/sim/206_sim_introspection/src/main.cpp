// ============================================================================
// Test HOST-206: introspeccion simulada (eng::sim + puente eng::board)
// ============================================================================
//
// Valida `eng/sim/introspection.hpp` (confianza, duda, presion, sorpresa, satisfaccion,
// alerta y conocimiento) y el puente `eng/board/persona.hpp` (hechos desde la busqueda y
// tras la jugada del rival), mas su efecto sobre el afecto/estado.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/206_sim_introspection

#include <cstdio>

#include <eng/board/persona.hpp>
#include <eng/sim/introspection.hpp>

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

void test_confident_vs_doubt() {
	// Margen grande entre la mejor y la segunda: confianza alta, sin duda.
	DecisionFacts clear {};
	clear.best_score = 300;
	clear.second_score = 100;
	clear.moves_available = 8u;
	const Introspection ci = introspect(clear);
	check(ci.confidence > 160u, "intro: margen grande = confianza");
	check(ci.doubt < 60u, "intro: margen grande = poca duda");

	// Margen minimo: duda alta, confianza baja.
	DecisionFacts unclear {};
	unclear.best_score = 50;
	unclear.second_score = 45;
	unclear.moves_available = 8u;
	const Introspection ui = introspect(unclear);
	check(ui.doubt > 100u, "intro: margen minimo = duda alta");
	check(ui.confidence < ci.confidence, "intro: dudar baja la confianza");

	// Jugada forzada: seguridad total.
	DecisionFacts forced {};
	forced.best_score = 0;
	forced.second_score = 0;
	forced.moves_available = 1u;
	const Introspection fi = introspect(forced);
	check(fi.confidence >= 160u && fi.doubt == 0u, "intro: forzada = confianza, sin duda");
}

void test_book_and_pressure() {
	// Consultar el libro sube la confianza.
	DecisionFacts f {};
	f.best_score = 50;
	f.second_score = 50;
	f.moves_available = 8u;
	DecisionFacts book = f;
	book.book_hit = true;
	check(introspect(book).confidence > introspect(f).confidence,
	      "intro: el libro sube la confianza");
	check(introspect(book).knowledge > 150u, "intro: el libro marca conocimiento");

	// Poco tiempo y desventaja: presion.
	DecisionFacts rushed = f;
	rushed.time_left = 10u;
	rushed.best_score = -500;
	check(introspect(rushed).pressure > 100u, "intro: prisa/desventaja = presion");
}

void test_surprise_and_alert() {
	// La posicion mejoro respecto a lo esperado: error del rival.
	DecisionFacts blunder {};
	blunder.best_score = 400;
	blunder.prev_eval = 0;
	check(introspect(blunder).alert > 80u, "intro: el error del rival da alerta");

	// La posicion empeoro: sorpresa.
	DecisionFacts surprised {};
	surprised.best_score = -300;
	surprised.prev_eval = 0;
	check(introspect(surprised).surprise > 60u, "intro: empeorar da sorpresa");
}

void test_satisfaction_and_affect() {
	// Jugada buena y clara: satisfaccion.
	DecisionFacts good {};
	good.best_score = 400;
	good.second_score = 50;
	good.moves_available = 8u;
	const Introspection gi = introspect(good);
	check(gi.satisfaction > 80u, "intro: jugada clara = satisfaccion");

	// La introspeccion mueve afecto y estado.
	Mind mind {};
	PsycheState s {};
	const u8 joy0 = mind.emotions.joy;
	introspection_apply(gi, mind, s);
	check(mind.emotions.joy > joy0, "intro: satisfaccion sube la alegria");

	Mind mind2 {};
	PsycheState s2 {};
	const Introspection pi = introspect([] {
		DecisionFacts d {};
		d.time_left = 5u;
		d.best_score = -800;
		d.second_score = -800;
		d.moves_available = 3u;
		return d;
	}());
	introspection_apply(pi, mind2, s2);
	check(mind2.emotions.fear > 0u && s2.tension > 0u,
	      "intro: la presion da miedo y tension");
}

void test_board_bridge() {
	// Puente desde la busqueda: mejor y segunda linea fijan el margen.
	const eng::board::Score scores[3] {250, 40, 30};
	const DecisionFacts f = eng::board::facts_from_search(250, eng::Span<const eng::board::Score> {scores, 3u},
	                                                      8u, false, 255u);
	check(f.best_score == 250 && f.second_score == 40, "puente: mejor y segunda linea");
	const Introspection in = introspect(f);
	check(in.confidence > 160u, "puente: margen claro = confianza");

	// Puente tras la jugada del rival: salto favorable = error.
	const DecisionFacts after = eng::board::facts_after_opponent(0, 400, 255u);
	check(introspect(after).alert > 80u, "puente: el rival se equivoco = alerta");
}

void test_dominant_name() {
	DecisionFacts rushed {};
	rushed.time_left = 0u;
	rushed.best_score = -900;
	check(introspection_dominant(introspect(rushed))[0] == 'p', "intro: nombre 'presionado'");
}

bool has_kind(const LeakList& list, GestureKind kind) {
	for (eng::usize i = 0u; i < list.size(); ++i) {
		if (list[i].kind == kind) {
			return true;
		}
	}
	return false;
}

void test_pace_gestures() {
	// Rival rapido: no hay gesto de espera.
	check(gestures_for_pace(0u).size() == 0u, "ritmo: sin espera no hay gestos");
	check(gestures_for_pace(30u).size() == 0u, "ritmo: espera corta sin gestos");

	// Espera media: impaciencia (pie/dedo) y resoplido.
	const LeakList mid = gestures_for_pace(150u);
	check(has_kind(mid, GestureKind::FootTap), "ritmo: espera media = pie");
	check(has_kind(mid, GestureKind::Sigh), "ritmo: espera media = resoplido");

	// Espera larga: bostezo y desplome.
	const LeakList long_wait = gestures_for_pace(220u);
	check(has_kind(long_wait, GestureKind::Yawn), "ritmo: espera larga = bostezo");
	check(has_kind(long_wait, GestureKind::Slump), "ritmo: espera larga = desplome");
}

} // namespace

int main() {
	std::printf("eng::sim introspection:\n");
	test_confident_vs_doubt();
	test_book_and_pressure();
	test_surprise_and_alert();
	test_satisfaction_and_affect();
	test_board_bridge();
	test_dominant_name();
	test_pace_gestures();

	if (g_fail == 0u) {
		std::printf("OK: eng::sim introspection (confianza, duda, presion, sorpresa, ritmo y puente con board)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
