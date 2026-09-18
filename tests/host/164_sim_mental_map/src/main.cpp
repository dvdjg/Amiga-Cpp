// ============================================================================
// Test HOST-164: mapa mental aplicado al movimiento (memoria espacial + astar/flujo)
// ============================================================================
//
// Valida `eng/sim/mental_map.hpp` y el olvido de lugares de `memory.hpp`:
//
//   1) `place_bias`: refugio/comida recordados abaratan; peligro encarece.
//   2) `MentalOverlay`: capa de coste por celda para `eng::util::astar` (combinada con el
//      `TerrainMap`).
//   3) `deposit_mental_danger`: marca el peligro recordado en un `InfluenceMap` para que
//      el steering/flow field lo evite.
//   4) `decay_places`/`forget_place`: el mapa mental se desactualiza y olvida (solo los
//      lugares, no las creencias sobre individuos).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/164_sim_mental_map

#include <cstdio>

#include <eng/sim/mental_map.hpp>
#include <eng/sim/memory.hpp>
#include <eng/sim/terrain.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// En una rejilla 4x4, la region de una celda es `idx/4`.
[[nodiscard]] RoomId room_of(eng::usize idx) {
	return static_cast<RoomId>(idx / 4u);
}

void test_bias() {
	KnowledgeSet kn;
	remember_place(kn, KnowledgeKind::Shelter, 1u, 200u);
	remember_place(kn, KnowledgeKind::Danger, 2u, 200u);
	remember_place(kn, KnowledgeKind::FoodSource, 3u, 150u);
	const MentalMapParams p {};
	check(place_bias(kn, 1u, p) < 0, "mapa mental: el refugio atrae");
	check(place_bias(kn, 2u, p) > 0, "mapa mental: el peligro disuade");
	check(place_bias(kn, 3u, p) < 0, "mapa mental: la comida atrae");
	check(place_bias(kn, 0u, p) == 0, "mapa mental: lo desconocido es neutro");
	check(place_bias(kn, no_room, p) == 0, "mapa mental: sin region no hay sesgo");
}

void test_overlay() {
	KnowledgeSet kn;
	remember_place(kn, KnowledgeKind::Shelter, 1u, 200u);
	remember_place(kn, KnowledgeKind::Danger, 2u, 200u);

	MentalOverlay<4, 4> overlay;
	overlay.stamp(kn, room_of);
	const eng::u16 neutral = overlay.cost(0u, 10u);
	const eng::u16 sheltered = overlay.cost(4u, 10u);
	const eng::u16 dangerous = overlay.cost(8u, 10u);
	check(neutral == 10u, "overlay: lo desconocido conserva el coste base");
	check(sheltered < neutral, "overlay: el refugio abarata la ruta fina");
	check(dangerous > neutral, "overlay: el peligro encarece la ruta fina");
	check(overlay.cost(0u, 1u) >= 1u, "overlay: el coste nunca baja de 1");
}

void test_influence() {
	KnowledgeSet kn;
	remember_place(kn, KnowledgeKind::Danger, 2u, 200u);
	eng::ai::InfluenceMap<4, 4> inf;
	deposit_mental_danger(kn, inf, room_of);
	check(inf.at(8u) > 0, "influencia: el peligro recordado se marca");
	check(inf.strongest() >= 8u && inf.strongest() <= 11u,
	      "influencia: la region peligrosa domina el mapa");
	check(inf.at(0u) == 0, "influencia: lo no peligroso no se marca");
}

void test_forget() {
	KnowledgeSet kn;
	remember_place(kn, KnowledgeKind::Shelter, 1u, 130u);
	remember_place(kn, KnowledgeKind::Danger, 2u, 60u);
	(void)learn(kn, KnowledgeKind::Enemy, 5u, 200u); // creencia sobre un individuo

	decay_places(kn, 50u);
	check(place_confidence(kn, KnowledgeKind::Shelter, 1u) == 80u &&
		      place_confidence(kn, KnowledgeKind::Danger, 2u) == 10u,
	      "olvido: los lugares decaen");
	check(confidence_for(kn, KnowledgeKind::Enemy, 5u) == 200u,
	      "olvido: las creencias sobre individuos no se tocan");

	decay_places(kn, 100u);
	check(best_known_room(kn, KnowledgeKind::Shelter) == no_room &&
		      find_knowledge(kn, KnowledgeKind::Danger, 2u) == nullptr,
	      "olvido: los lugares antiguos desaparecen");

	forget_place(kn, KnowledgeKind::Enemy, 5u);
	check(find_knowledge(kn, KnowledgeKind::Enemy, 5u) == nullptr,
	      "olvido: se puede olvidar un lugar a mano");
}

} // namespace

int main() {
	std::printf("Sim mental map:\n");
	test_bias();
	test_overlay();
	test_influence();
	test_forget();

	if (g_fail == 0u) {
		std::printf("OK: Sim mental map (sesgo, overlay para astar, influencia, olvido)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
