// ============================================================================
// Test HOST-166: mapa mental en el mundo (ruta fina y flujo)
// ============================================================================
//
// Valida que `SimWorld` expone el mapa mental de una criatura para el pathfinding fino y
// el campo de influencia, sin que el juego tenga que conocer los detalles:
//
//   1) `mental_bias`: sesgo de una region segun lo recordado.
//   2) `stamp_mental_overlay` + `TerrainMap`: coste combinado para `eng::util::astar`.
//   3) `stamp_mental_danger`: peligro recordado en un `InfluenceMap`.
//   4) La ruta fina (astar) sigue encontrando paso pero penalizando el peligro recordado.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/166_sim_world_mental_routes

#include <cstdio>

#include <eng/core/util/pathfinding.hpp>
#include <eng/sim/mental_map.hpp>
#include <eng/sim/terrain.hpp>
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

[[nodiscard]] RoomId room_of(eng::usize idx) {
	return static_cast<RoomId>(idx / 4u);
}

void test_bias_and_layers() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	remember_place(w.find(a)->knowledge, KnowledgeKind::Shelter, 1u, 200u);
	remember_place(w.find(a)->knowledge, KnowledgeKind::Danger, 2u, 200u);
	remember_place(w.find(a)->knowledge, KnowledgeKind::FoodSource, 3u, 160u);

	check(w.mental_bias(a, 1u) < 0 && w.mental_bias(a, 2u) > 0 && w.mental_bias(a, 3u) < 0,
	      "world: el sesgo mental refleja el mapa");
	check(w.mental_bias(a, 0u) == 0, "world: lo no recordado es neutro");

	MentalOverlay<4, 4> overlay;
	w.stamp_mental_overlay(a, overlay, room_of);
	check(overlay.cost(4u, 10u) < 10u, "world: el refugio abarata la celda");
	check(overlay.cost(8u, 10u) > 10u, "world: el peligro encarece la celda");

	eng::ai::InfluenceMap<4, 4> inf;
	w.stamp_mental_danger(a, inf, room_of);
	check(inf.strongest() >= 8u && inf.strongest() <= 11u,
	      "world: el peligro recordado domina la influencia");
}

void test_fine_route() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	remember_place(w.find(a)->knowledge, KnowledgeKind::Danger, 1u, 200u);

	TerrainMap<4, 4> map;
	map.fill(TerrainKind::Floor);
	MentalOverlay<4, 4> overlay;
	w.stamp_mental_overlay(a, overlay, room_of);

	eng::s16 came[16];
	eng::u16 gs[16];
	eng::u8 closed[16];
	// Coste combinado terreno + mapa mental (peligro recordado en la fila 1).
	const bool found = eng::util::astar<4, 4>(
		0u, 15u,
		[&](eng::u16 idx) { return map.walkable(idx, movement::walk); },
		[&](eng::u16, eng::u16 to) { return overlay.cost(to, map.cost(to)); },
		eng::Span<eng::s16> {came, 16}, eng::Span<eng::u16> {gs, 16},
		eng::Span<eng::u8> {closed, 16});
	check(found, "world: la ruta fina con el mapa mental encuentra paso");
	check(overlay.cost(4u, 1u) > 1u, "world: el coste refleja el peligro recordado");
}

} // namespace

int main() {
	std::printf("Sim world mental routes:\n");
	test_bias_and_layers();
	test_fine_route();

	if (g_fail == 0u) {
		std::printf("OK: Sim world mental routes (sesgo, overlay para astar, influencia)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
