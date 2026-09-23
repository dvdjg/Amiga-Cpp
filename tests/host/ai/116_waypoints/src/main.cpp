// ============================================================================
// Test HOST-116: grafo de waypoints con A*
// ============================================================================
//
// Valida `engine/include/eng/ai/navigation/waypoints.hpp`:
//
//   1) Ruta optima en un grafo en diamante (elige la rama mas barata).
//   2) Nodo inalcanzable -> sin camino.
//   3) Capacidad de nodos/aristas.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/116_waypoints

#include <cstdio>

#include <eng/ai/navigation/waypoints.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::s16;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using Graph = eng::ai::WaypointGraph<eng::s32, 8, 8>;
u16 g_g[8] {};
s16 g_came[8] {};
u8 g_closed[8] {};
u16 g_path[8] {};

void test_diamond() {
	Graph graph;
	const u16 a = graph.add_node({0, 0});
	const u16 b = graph.add_node({10, 0});
	const u16 c = graph.add_node({20, 0});
	const u16 d = graph.add_node({10, 10});
	check(a == 0u && b == 1u && c == 2u && d == 3u, "waypoints: indices de nodo");
	check(graph.add_edge(a, b, 10u), "waypoints: arista a-b");
	check(graph.add_edge(b, c, 10u), "waypoints: arista b-c");
	check(graph.add_edge(a, d, 15u), "waypoints: arista a-d");
	check(graph.add_edge(d, c, 15u), "waypoints: arista d-c");
	check(graph.node_count() == 4u && graph.edge_count() == 4u, "waypoints: 4 nodos y 4 aristas");

	// a -> c: por b cuesta 20, por d cuesta 30. Gana b.
	const usize n = graph.find_path(a, c, g_g, g_came, g_closed, g_path);
	check(n == 3u, "waypoints: camino de 3 nodos");
	check(g_path[0] == a && g_path[1] == b && g_path[2] == c, "waypoints: ruta optima por b");
	check(g_g[c] == 20u, "waypoints: coste minimo 20");
}

void test_unreachable() {
	Graph graph;
	const u16 a = graph.add_node({0, 0});
	const u16 b = graph.add_node({1, 0});
	check(graph.add_edge(a, b, 1u), "waypoints: arista a-b del caso aislado");
	const u16 isolated = graph.add_node({50, 50});

	const usize n = graph.find_path(a, isolated, g_g, g_came, g_closed, g_path);
	check(n == 0u, "waypoints: nodo aislado sin camino");
	check(graph.find_path(a, a, g_g, g_came, g_closed, g_path) == 1u,
	      "waypoints: camino trivial de un nodo");
}

void test_capacity() {
	Graph graph;
	for (u16 i = 0; i < 8u; ++i) {
		check(graph.add_node({static_cast<s16>(i), 0}) != Graph::no_node,
		      "waypoints: caben 8 nodos");
	}
	check(graph.add_node({9, 0}) == Graph::no_node, "waypoints: el noveno no cabe");
}

} // namespace

int main() {
	std::printf("Waypoints:\n");
	test_diamond();
	test_unreachable();
	test_capacity();

	if (g_fail == 0u) {
		std::printf("OK: WaypointGraph (ruta optima, inalcanzable, capacidad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
