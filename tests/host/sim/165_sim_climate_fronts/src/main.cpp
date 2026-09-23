// ============================================================================
// Test HOST-165: clima con frentes (difusion entre regiones vecinas)
// ============================================================================
//
// Valida `SimWorld::diffuse_climate` y `Climate`:
//
//   1) El peligro salta una fraccion a las regiones **adyacentes** (y solo a esas).
//   2) Repetido, el frente **avanza** por el grafo conservando el `HazardKind`.
//   3) `decay` lo disipa y limpia el tipo cuando llega a 0.
//   4) `set_hazard` sigue fijando un peligro global (todas las regiones).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/165_sim_climate_fronts

#include <cstdio>

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

void test_fronts() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.link_rooms(0u, 1u);
	w.link_rooms(1u, 2u);
	w.climate().set(0u, HazardKind::Storm, 200u);

	w.diffuse_climate(50u, 0u);
	check(w.climate().severity(0u) == 200u, "frente: el origen no cambia al propagar");
	check(w.climate().severity(1u) == 100u, "frente: el vecino recibe la fraccion");
	check(w.climate().severity(2u) == 0u, "frente: a dos saltos aun no llega");

	w.diffuse_climate(50u, 0u);
	check(w.climate().severity(2u) > 0u, "frente: el frente avanza al segundo salto");
	check(w.climate().at(2u).kind == HazardKind::Storm,
	      "frente: conserva el tipo de peligro");
	check(w.climate().severity(0u) > 200u, "frente: el origen se refuerza desde el vecino");
}

void test_isolated() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.link_rooms(0u, 1u);
	// La region 5 no esta conectada.
	w.climate().set(0u, HazardKind::Cold, 200u);
	for (int i = 0; i < 4; ++i) {
		w.diffuse_climate(50u, 0u);
	}
	check(w.climate().severity(5u) == 0u, "frente: no salta a una region aislada");
	check(w.climate().severity(1u) > 0u, "frente: si llega al vecino conectado");
}

void test_decay() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.link_rooms(0u, 1u);
	w.climate().set(0u, HazardKind::Heat, 200u);
	w.diffuse_climate(40u, 30u); // siembra 80 en el vecino y disipa 30 en todas
	check(w.climate().severity(0u) == 170u, "frente: el origen se disipa");
	check(w.climate().severity(1u) == 50u, "frente: el vecino recibe y conserva el tipo");
	check(w.climate().at(1u).kind == HazardKind::Heat, "frente: el vecino adopta el tipo");

	w.diffuse_climate(0u, 100u);
	check(w.climate().severity(1u) == 0u && w.climate().at(1u).kind == HazardKind::None,
	      "frente: al disiparse se limpia el tipo");
}

void test_global() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.set_hazard(HazardKind::Rain, 150u);
	check(w.climate().severity(0u) == 150u && w.climate().severity(7u) == 150u,
	      "frente: set_hazard fija el peligro en todas las regiones");
	check(w.environment_severe() && w.hazard() == HazardKind::Rain,
	      "frente: consultas globales coherentes");
}

} // namespace

int main() {
	std::printf("Sim climate fronts:\n");
	test_fronts();
	test_isolated();
	test_decay();
	test_global();

	if (g_fail == 0u) {
		std::printf("OK: Sim climate fronts (propagacion, avance, disipacion, global)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
