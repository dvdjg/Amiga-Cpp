// ============================================================================
// Test HOST-175: entrada humana del avatar (el humano sustituye a la IA)
// ============================================================================
//
// Valida `player_control` de `eng/sim/avatar.hpp`: la misma capa que mueve al jugador con
// IA (`player_step`) acepta **entrada humana** sin tocar el mundo.
//
//   1) Movimiento por `PlayerInput` (dx/dy) y recorte a los limites del mundo.
//   2) Interaccion (comer/usar) solo donde hay recurso; descanso.
//   3) El mundo sigue coherente tras la entrada (busqueda por id intacta).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/175_sim_player_input

#include <cstdio>

#include <eng/sim/avatar.hpp>
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

using World = SimWorld<SimTraits, 16, 4, 4, 8>;

void test_move() {
	World w;
	w.link_rooms(0u, 1u);
	const EntityId p = w.spawn(9u, 0u, 0u, 5, 5);
	check(!player_control(w, p, PlayerInput {3, -2, false, false}),
	      "input: moverse no es actuar");
	check(w.find(p)->x == 8 && w.find(p)->y == 3, "input: aplica el desplazamiento");

	(void)player_control(w, p, PlayerInput {100, 0, false, false});
	check(w.find(p)->x == 40, "input: recorta al limite superior");
	(void)player_control(w, p, PlayerInput {-500, -500, false, false});
	check(w.find(p)->x == 0 && w.find(p)->y == 0, "input: recorta al limite inferior");

	check(!player_control(w, 999u, PlayerInput {1, 1, false, false}),
	      "input: id inexistente no hace nada");
}

void test_interact() {
	World w;
	w.link_rooms(0u, 1u);
	w.set_biome(0u, BiomeKind::Forest);  // comida 80
	w.set_biome(1u, BiomeKind::Desert);  // comida 15
	const EntityId p = w.spawn(9u, 0u, 0u, 5, 5);
	w.find(p)->needs.hunger = 200u;
	w.find(p)->needs.fatigue = 200u;

	check(player_control(w, p, PlayerInput {0, 0, true, false}) &&
		      w.find(p)->needs.hunger < 200u,
	      "input: interactua (come) donde hay comida");

	const eng::u8 after_food = w.find(p)->needs.hunger;
	w.find(p)->room = 1u; // desierto
	check(!player_control(w, p, PlayerInput {0, 0, true, false}) &&
		      w.find(p)->needs.hunger == after_food,
	      "input: sin recurso, interactuar no hace nada");

	w.find(p)->room = 0u;
	check(player_control(w, p, PlayerInput {0, 0, false, true}) &&
		      w.find(p)->needs.fatigue < 200u,
	      "input: descansar recupera energia");
}

void test_parity() {
	// IA y humano mueven al mismo avatar por caminos distintos; el mundo queda coherente.
	World w;
	w.set_biome(0u, BiomeKind::Forest);
	const EntityId p = w.spawn(9u, 0u, 0u, 5, 5);
	w.find(p)->needs.hunger = 200u;
	eng::Xoroshiro64pp rng {5u, 6u};
	const PlayerIntent it = player_step(w, p, rng);
	check(it == PlayerIntent::Feed, "parity: la IA elige comer");
	(void)player_control(w, p, PlayerInput {1, 1, true, false});
	check(w.find(p) != nullptr && w.find(p)->alive(), "parity: el mundo sigue coherente");
}

} // namespace

int main() {
	std::printf("Sim player input:\n");
	test_move();
	test_interact();
	test_parity();

	if (g_fail == 0u) {
		std::printf("OK: Sim player input (movimiento, interaccion, paridad IA/humano)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
