// ============================================================================
// Test HOST-163: mapa mental y rutas macro (memoria espacial en el pathfinding)
// ============================================================================
//
// Valida la memoria espacial de `eng/sim/memory.hpp` y su uso en `SimWorld`:
//
//   1) `remember_place`/`best_known_room`/`place_confidence`: recordar regiones.
//   2) `route_room`/`route_first_step`: BFS sobre el grafo de regiones (macro pathfinding).
//   3) `preferred_refuge`: refugio recordado (mapa mental) o guarida.
//   4) Migracion off-screen al refugio recordado con clima adverso; y consolidacion de un
//      refugio percibido (Den) a memoria de largo plazo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/163_sim_spatial_memory

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

void test_places() {
	KnowledgeSet kn;
	check(best_known_room(kn, KnowledgeKind::Shelter) == no_room,
	      "lugares: sin recuerdos no hay refugio");
	remember_place(kn, KnowledgeKind::Shelter, 3u, 130u);
	check(place_confidence(kn, KnowledgeKind::Shelter, 3u) == 130u &&
		      best_known_room(kn, KnowledgeKind::Shelter) == 3u,
	      "lugares: se recuerda la region");
	remember_place(kn, KnowledgeKind::Shelter, 5u, 200u);
	check(best_known_room(kn, KnowledgeKind::Shelter) == 5u,
	      "lugares: gana la region con mas confianza");
	check(best_known_room(kn, KnowledgeKind::Shelter, 220u) == no_room,
	      "lugares: por debajo del umbral no cuenta");
}

void test_routes() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.link_rooms(0u, 1u);
	w.link_rooms(1u, 2u);
	w.link_rooms(2u, 3u);

	RoomId path[8];
	const eng::u8 n = w.route_room(0u, 3u, eng::Span<RoomId> {path, 8});
	check(n == 4u && path[0] == 0u && path[1] == 1u && path[2] == 2u && path[3] == 3u,
	      "rutas: BFS encadena las regiones");
	check(w.route_first_step(0u, 3u) == 1u, "rutas: primer paso correcto");
	check(w.route_first_step(3u, 3u) == no_room, "rutas: ya estas en el destino");

	// Region aislada: sin ruta.
	check(w.route_room(0u, 7u, eng::Span<RoomId> {path, 8}) == 0u,
	      "rutas: region inalcanzable");
}

void test_refuge_and_migration() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.link_rooms(0u, 1u);
	w.link_rooms(1u, 2u);
	w.link_rooms(2u, 3u);
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);

	// Sin memoria: el refugio por defecto es la guarida.
	w.find(a)->set_den(1u, 0, 0);
	check(w.preferred_refuge(*w.find(a)) == 1u, "refugio: sin memoria usa la guarida");

	// Con mapa mental: una region recordada como refugio manda.
	remember_place(w.find(a)->knowledge, KnowledgeKind::Shelter, 3u, 200u);
	check(w.preferred_refuge(*w.find(a)) == 3u, "refugio: manda el recuerdo");

	// Migracion off-screen hacia el refugio recordado con clima adverso.
	w.set_hazard(HazardKind::Storm, 220u);
	eng::Xoroshiro64pp rng {5u, 6u};
	for (int i = 0; i < 16; ++i) {
		w.tick_abstract(rng);
	}
	check(w.find(a)->room == 3u, "refugio: migra al lugar recordado");

	// Consolidacion: un refugio percibido (Den) pasa a memoria de largo plazo.
	SimWorld<SimTraits, 8, 4, 4, 8> w2;
	const EntityId b = w2.spawn(1u, 0u, 0u, 0, 0);
	observe(w2.find(b)->trackers, TrackerKind::Den, 9u, 6u, 1, 1, 200u, 0u);
	MemoryParams mp {};
	mp.consolidation_threshold = 100u;
	w2.set_memory_params(mp);
	check(w2.consolidate_memory(b) > 0u &&
		      best_known_room(w2.find(b)->knowledge, KnowledgeKind::Shelter) == 6u,
	      "refugio: lo percibido se consolida y guia la migracion");
	check(w2.preferred_refuge(*w2.find(b)) == 6u,
	      "refugio: el lugar consolidado es el nuevo refugio preferido");
}

} // namespace

int main() {
	std::printf("Sim spatial memory:\n");
	test_places();
	test_routes();
	test_refuge_and_migration();

	if (g_fail == 0u) {
		std::printf("OK: Sim spatial memory (lugares, rutas macro, refugio recordado)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
