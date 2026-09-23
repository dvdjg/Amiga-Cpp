// ============================================================================
// Test HOST-112: Utility AI (puntuacion ponderada y selector)
// ============================================================================
//
// Valida `engine/include/eng/ai/decision/utility.hpp`:
//
//   1) `Utility`: media ponderada de consideraciones normalizadas [0, 1000],
//      recorte de valores y pesos no positivos ignorados.
//   2) `UtilitySelector`: mejor opcion, empate -> indice menor, capacidad y vacio.
//   3) Caso de uso: un guardia elige atacar o huir segun distancia y salud.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/112_utility

#include <cstdio>

#include <eng/ai/decision/utility.hpp>

namespace {

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_weighted_mean() {
	eng::ai::Utility u;
	check(u.empty() && u.score() == 0, "utility: sin consideraciones -> 0");

	// (800*2 + 200*1) / 3 = 600.
	u.add(800, 2);
	u.add(200, 1);
	check(u.score() == 600, "utility: media ponderada");

	// Recorte: -100 -> 0 y 5000 -> 1000; el peso 0 se ignora.
	eng::ai::Utility c;
	c.add(-100, 1);
	c.add(5000, 1);
	check(c.score() == 500, "utility: recorta los valores al rango");

	eng::ai::Utility w;
	w.add(700, 0);
	w.add(700, -3);
	check(w.empty(), "utility: pesos no positivos se ignoran");
	w.add(1000, 1);
	check(w.score() == 1000, "utility: una sola consideracion");
}

void test_selector() {
	eng::ai::UtilitySelector<3> sel;
	check(sel.empty() && sel.best() == eng::ai::UtilitySelector<3>::no_option,
	      "selector: vacio -> no_option");

	check(sel.add(600) && sel.add(300), "selector: caben las opciones");
	check(sel.best() == 0u, "selector: gana la mayor puntuacion");

	sel.reset();
	sel.add(400);
	sel.add(400);
	check(sel.best() == 0u, "selector: empate -> indice menor");

	eng::ai::UtilitySelector<2> full;
	check(full.add(1) && full.add(2), "selector: caben dos");
	check(!full.add(3), "selector: la tercera no cabe");
}

void test_guard_decision() {
	// Opcion 0: atacar (cerca y con municion). Opcion 1: huir (salud baja).
	eng::ai::Utility attack;
	attack.add(900, 2); // distancia muy buena
	attack.add(700, 1); // municion aceptable

	eng::ai::Utility flee;
	flee.add(200, 1); // salud razonable -> huir tiene poco atractivo

	eng::ai::UtilitySelector<2> sel;
	sel.add(attack.score());
	sel.add(flee.score());
	check(sel.best() == 0u, "guardia: con salud ataca");
	check(attack.score() == (900 * 2 + 700) / 3, "guardia: utilidad de atacar");

	// Salud muy baja (100) y enemigo lejos (200): ahora conviene huir.
	eng::ai::Utility attack2;
	attack2.add(200, 2);
	attack2.add(700, 1);
	eng::ai::Utility flee2;
	flee2.add(1000, 2);

	eng::ai::UtilitySelector<2> sel2;
	sel2.add(attack2.score());
	sel2.add(flee2.score());
	check(sel2.best() == 1u, "guardia: herido y lejos huye");
}

} // namespace

int main() {
	std::printf("Utility:\n");
	test_weighted_mean();
	test_selector();
	test_guard_decision();

	if (g_fail == 0u) {
		std::printf("OK: Utility (media ponderada, selector, decision del guardia)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
