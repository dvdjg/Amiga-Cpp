// ============================================================================
// Test HOST-126: grafo generico (adyacencia, BFS, A*, orden topologico)
// ============================================================================
//
// Valida `engine/include/eng/core/util/graph.hpp`: `Graph<MaxNodes,MaxEdges>` (listas de
// adyacencia de capacidad fija) y sus algoritmos.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/126_graph

#include <cstdio>

#include <eng/core/util/graph.hpp>

namespace {

using eng::u16;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

using G = eng::util::Graph<8, 12>;

void test_adjacency_and_bfs() {
	G g;
	const u16 a = g.add_node();
	const u16 b = g.add_node();
	const u16 c = g.add_node();
	check(a == 0u && b == 1u && c == 2u, "grafo: indices de nodo");
	check(g.add_edge(a, b, 4u), "grafo: arista a-b");
	check(g.add_edge(b, c, 5u), "grafo: arista b-c");
	check(g.node_count() == 3u && g.edge_count() == 4u, "grafo: 2 aristas bidireccionales = 4");

	unsigned neighbors = 0u;
	g.for_each_neighbor(a, [&](u16, u16) { ++neighbors; });
	check(neighbors == 1u, "grafo: a tiene 1 vecino");

	eng::s16 came[8] {};
	u16 queue[8] {};
	u16 out[8] {};
	const usize n = eng::util::graph_bfs(g, a, c, came, queue, out);
	check(n == 3u && out[0] == a && out[1] == b && out[2] == c, "grafo: BFS 0-1-2");
}

void test_astar_optimal() {
	G g;
	for (int i = 0; i < 5; ++i) {
		(void)g.add_node();
	}
	// Ruta barata 0->1->3->4 y ruta cara 0->2->3->4.
	(void)g.add_edge(0u, 1u, 1u);
	(void)g.add_edge(1u, 3u, 1u);
	(void)g.add_edge(3u, 4u, 1u);
	(void)g.add_edge(0u, 2u, 5u);
	(void)g.add_edge(2u, 3u, 5u);

	u16 gs[8] {};
	eng::s16 came[8] {};
	eng::u8 closed[8] {};
	u16 out[8] {};
	const auto zero_h = [](u16, u16) { return static_cast<u16>(0u); }; // Dijkstra
	const usize n = eng::util::graph_astar(g, 0u, 4u, zero_h, gs, came, closed, out);
	check(n == 4u, "grafo: A* encuentra camino de 4 nodos");
	check(out[0] == 0u && out[1] == 1u && out[2] == 3u && out[3] == 4u, "grafo: ruta barata");
	check(gs[4] == 3u, "grafo: coste minimo 3");
}

void test_topological_sort() {
	G g;
	for (int i = 0; i < 6; ++i) {
		(void)g.add_node();
	}
	(void)g.add_edge(0u, 1u, 1u, false);
	(void)g.add_edge(0u, 2u, 1u, false);
	(void)g.add_edge(1u, 3u, 1u, false);
	(void)g.add_edge(2u, 3u, 1u, false);
	(void)g.add_edge(3u, 4u, 1u, false);
	(void)g.add_edge(2u, 5u, 1u, false);

	u16 indegree[8] {};
	u16 queue[8] {};
	u16 out[8] {};
	const usize n = eng::util::topological_sort(g, indegree, queue, out);
	check(n == 6u, "grafo: DAG -> 6 nodos ordenados");

	// Validar el orden: cada arista va de posicion menor a mayor.
	u16 pos[8] {};
	for (usize i = 0; i < n; ++i) {
		pos[out[i]] = static_cast<u16>(i);
	}
	bool ok = true;
	for (u16 node = 0u; node < 6u; ++node) {
		g.for_each_neighbor(node, [&](u16 nb, u16) {
			if (pos[node] >= pos[nb]) {
				ok = false;
			}
		});
	}
	check(ok, "grafo: orden topologico valido");

	// Con un ciclo, Kahn no cubre todos los nodos.
	G cyc;
	for (int i = 0; i < 3; ++i) {
		(void)cyc.add_node();
	}
	(void)cyc.add_edge(0u, 1u, 1u, false);
	(void)cyc.add_edge(1u, 2u, 1u, false);
	(void)cyc.add_edge(2u, 0u, 1u, false);
	u16 indeg2[8] {};
	u16 q2[8] {};
	u16 o2[8] {};
	check(eng::util::topological_sort(cyc, indeg2, q2, o2) == 0u,
	      "grafo: ciclo -> 0 nodos ordenables");
}

void test_capacity() {
	eng::util::Graph<2, 2> g;
	check(g.add_node() == 0u && g.add_node() == 1u, "grafo: caben 2 nodos");
	check(g.add_node() == eng::util::Graph<2, 2>::no_node, "grafo: el tercero no cabe");
	check(g.add_edge(0u, 1u, 1u), "grafo: cabe la arista");
	check(!g.add_edge(0u, 1u, 1u), "grafo: sin aristas libres -> false");
}

} // namespace

int main() {
	std::printf("Graph:\n");
	test_adjacency_and_bfs();
	test_astar_optimal();
	test_topological_sort();
	test_capacity();

	if (g_fail == 0u) {
		std::printf("OK: Graph (adyacencia, BFS, A*, orden topologico, capacidad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
