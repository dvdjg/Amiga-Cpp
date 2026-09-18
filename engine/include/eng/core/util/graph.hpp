#pragma once

/// \file graph.hpp
/// `eng::util::Graph<MaxNodes, MaxEdges>`: **grafo dirigido o bidireccional** con nodos
/// identificados por `u16` y aristas con coste, en listas de adyacencia de capacidad
/// fija (sin heap). Sobre él, algoritmos genéricos: **BFS**, **A\*** (heurística del
/// llamador) y **orden topológico** (Kahn).
///
/// Es la pieza que unifica navegación (grafos de waypoints), dependencias de assets y
/// árboles de tecnología: el dominio aporta posiciones/etiquetas y la heurística, y el
/// grafo la conectividad y la búsqueda.
///
/// Uso:
///   eng::util::Graph<16, 24> g;
///   const eng::u16 a = g.add_node();
///   const eng::u16 b = g.add_node();
///   g.add_edge(a, b, 10);              // bidireccional
///   g.for_each_neighbor(a, [](eng::u16 nb, eng::u16 cost) { ... });
///
/// Verificación: HOST-126.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/priority_queue.hpp>

namespace eng::util {

template <eng::u16 MaxNodes, eng::u16 MaxEdges>
class Graph {
	static_assert(MaxNodes > 0u, "Graph: MaxNodes debe ser mayor que 0");
	static_assert(MaxEdges > 0u, "Graph: MaxEdges debe ser mayor que 0");

public:
	static constexpr eng::u16 no_node = 0xffffu;

	[[nodiscard]] constexpr eng::u16 node_count() const noexcept { return m_nodes; }
	[[nodiscard]] constexpr eng::u16 edge_count() const noexcept { return m_edge_count; }

	constexpr void clear() noexcept {
		m_nodes = 0u;
		m_edge_count = 0u;
		for (eng::u16 i = 0u; i < MaxNodes; ++i) {
			m_head[i] = no_node;
		}
	}

	/// Añade un nodo aislado; devuelve su índice o `no_node` si no cabe.
	[[nodiscard]] constexpr eng::u16 add_node() noexcept {
		if (m_nodes >= MaxNodes) {
			return no_node;
		}
		m_head[m_nodes] = no_node;
		return m_nodes++;
	}

	/// Añade una arista `a -> b` (y `b -> a` si `bidirectional`). `false` si no cabe o
	/// los nodos no existen.
	[[nodiscard]] constexpr bool add_edge(eng::u16 a, eng::u16 b, eng::u16 cost,
					      bool bidirectional = true) noexcept {
		if (a >= m_nodes || b >= m_nodes || a == b) {
			return false;
		}
		const eng::u16 need = bidirectional ? 2u : 1u;
		if (static_cast<eng::u32>(m_edge_count) + need > MaxEdges) {
			return false; // no parte la arista bidireccional por la mitad
		}
		if (!push_edge(a, b, cost)) {
			return false;
		}
		if (bidirectional && !push_edge(b, a, cost)) {
			return false;
		}
		return true;
	}

	/// Recorre los vecinos de `node` llamando `fn(neighbor, cost)`.
	template <class Fn>
	constexpr void for_each_neighbor(eng::u16 node, Fn fn) const {
		if (node >= m_nodes) {
			return;
		}
		for (eng::u16 e = m_head[node]; e != no_node; e = m_edges[e].next) {
			fn(m_edges[e].to, m_edges[e].cost);
		}
	}

private:
	struct Edge {
		eng::u16 to;
		eng::u16 cost;
		eng::u16 next;
	};

	[[nodiscard]] constexpr bool push_edge(eng::u16 from, eng::u16 to,
					       eng::u16 cost) noexcept {
		if (m_edge_count >= MaxEdges) {
			return false;
		}
		m_edges[m_edge_count] = Edge {to, cost, m_head[from]};
		m_head[from] = m_edge_count;
		++m_edge_count;
		return true;
	}

	Edge m_edges[MaxEdges] {};
	eng::u16 m_head[MaxNodes] {};
	eng::u16 m_nodes = 0u;
	eng::u16 m_edge_count = 0u;
};

namespace detail {

struct GraphNode {
	eng::u16 idx;
	eng::u16 f;
};

struct GraphCmp {
	[[nodiscard]] constexpr bool operator()(const GraphNode& a,
						const GraphNode& b) const noexcept {
		if (a.f != b.f) {
			return a.f > b.f;
		}
		return a.idx > b.idx;
	}
};

/// Reconstruye `start..goal` desde `came_from` (como lo dejan `graph_bfs`/`graph_astar`).
[[nodiscard]] constexpr eng::usize graph_reconstruct(eng::u16 start, eng::u16 goal,
						     eng::Span<eng::s16> came_from,
						     eng::Span<eng::u16> out) noexcept {
	eng::usize count = 0u;
	eng::u16 n = goal;
	for (;;) {
		if (count >= out.size()) {
			return 0u;
		}
		out[count++] = n;
		if (n == start) {
			break;
		}
		const eng::s16 prev = came_from[n];
		if (prev < 0) {
			return 0u;
		}
		n = static_cast<eng::u16>(prev);
	}
	for (eng::usize i = 0u; i < count / 2u; ++i) {
		const eng::u16 tmp = out[i];
		out[i] = out[count - 1u - i];
		out[count - 1u - i] = tmp;
	}
	return count;
}

} // namespace detail

/// BFS sin peso en `graph` de `start` a `goal`. `queue` es scratch del llamador
/// (`MaxNodes`). Rellena `came_from` (`start` -> sí mismo; `-1` = no visitado) y el
/// camino en `out`. Devuelve la longitud, o `0`.
template <eng::u16 MaxNodes, eng::u16 MaxEdges>
[[nodiscard]] constexpr eng::usize graph_bfs(const Graph<MaxNodes, MaxEdges>& graph,
					     eng::u16 start, eng::u16 goal,
					     eng::Span<eng::s16> came_from,
					     eng::Span<eng::u16> queue,
					     eng::Span<eng::u16> out) noexcept {
	constexpr eng::usize N = MaxNodes;
	if (came_from.size() < N || queue.size() < N || out.empty()) {
		return 0u;
	}
	if (start >= graph.node_count() || goal >= graph.node_count()) {
		return 0u;
	}
	for (eng::usize i = 0; i < N; ++i) {
		came_from[i] = -1;
	}
	eng::usize head = 0u;
	eng::usize tail = 0u;
	came_from[start] = static_cast<eng::s16>(start);
	queue[tail++] = start;
	while (head < tail) {
		const eng::u16 cur = queue[head++];
		if (cur == goal) {
			return detail::graph_reconstruct(start, goal, came_from, out);
		}
		graph.for_each_neighbor(cur, [&](eng::u16 nb, eng::u16) {
			if (came_from[nb] == -1 && tail < N) {
				came_from[nb] = static_cast<eng::s16>(cur);
				queue[tail++] = nb;
			}
		});
	}
	return 0u;
}

/// A* en `graph` de `start` a `goal` con coste por arista y heurística del llamador
/// (`heuristic(node, goal) -> u16`, admisible). Mismo `scratch` que `eng::util::astar`.
template <eng::u16 MaxNodes, eng::u16 MaxEdges, class Heuristic>
[[nodiscard]] constexpr eng::usize graph_astar(const Graph<MaxNodes, MaxEdges>& graph,
					       eng::u16 start, eng::u16 goal,
					       Heuristic heuristic,
					       eng::Span<eng::u16> g_score,
					       eng::Span<eng::s16> came_from,
					       eng::Span<eng::u8> closed,
					       eng::Span<eng::u16> out) noexcept {
	constexpr eng::usize N = MaxNodes;
	if (g_score.size() < N || came_from.size() < N || closed.size() < N || out.empty()) {
		return 0u;
	}
	if (start >= graph.node_count() || goal >= graph.node_count()) {
		return 0u;
	}
	for (eng::usize i = 0; i < N; ++i) {
		g_score[i] = 0xffffu;
		came_from[i] = -1;
		closed[i] = 0u;
	}

	PriorityQueue<detail::GraphNode, N, detail::GraphCmp> open;
	g_score[start] = 0u;
	came_from[start] = static_cast<eng::s16>(start);
	open.push(detail::GraphNode {start, heuristic(start, goal)});

	while (!open.empty()) {
		const detail::GraphNode cur = open.top();
		open.pop();
		if (closed[cur.idx] != 0u) {
			continue;
		}
		closed[cur.idx] = 1u;
		if (cur.idx == goal) {
			return detail::graph_reconstruct(start, goal, came_from, out);
		}
		graph.for_each_neighbor(cur.idx, [&](eng::u16 nb, eng::u16 cost) {
			if (closed[nb] != 0u) {
				return;
			}
			const eng::u32 ng = static_cast<eng::u32>(g_score[cur.idx]) + cost;
			if (ng < g_score[nb]) {
				g_score[nb] = static_cast<eng::u16>(ng > 0xffffu ? 0xffffu : ng);
				came_from[nb] = static_cast<eng::s16>(cur.idx);
				const eng::u32 f = ng + heuristic(nb, goal);
				open.push(detail::GraphNode {
					nb, static_cast<eng::u16>(f > 0xffffu ? 0xffffu : f)});
			}
		});
	}
	return 0u;
}

/// Orden topológico (Kahn) de un grafo dirigido acíclico. `indegree` y `queue` son
/// scratch (`MaxNodes`). Escribe los nodos en `out` y devuelve cuántos; si es menor que
/// `node_count()`, hay un ciclo.
template <eng::u16 MaxNodes, eng::u16 MaxEdges>
[[nodiscard]] constexpr eng::usize topological_sort(const Graph<MaxNodes, MaxEdges>& graph,
						    eng::Span<eng::u16> indegree,
						    eng::Span<eng::u16> queue,
						    eng::Span<eng::u16> out) noexcept {
	constexpr eng::usize N = MaxNodes;
	if (indegree.size() < N || queue.size() < N) {
		return 0u;
	}
	for (eng::usize i = 0; i < N; ++i) {
		indegree[i] = 0u;
	}
	for (eng::u16 n = 0u; n < graph.node_count(); ++n) {
		graph.for_each_neighbor(n, [&](eng::u16 nb, eng::u16) {
			++indegree[nb];
		});
	}
	eng::usize head = 0u;
	eng::usize tail = 0u;
	for (eng::u16 n = 0u; n < graph.node_count(); ++n) {
		if (indegree[n] == 0u) {
			queue[tail++] = n;
		}
	}
	eng::usize count = 0u;
	while (head < tail) {
		const eng::u16 cur = queue[head++];
		if (count >= out.size()) {
			return 0u;
		}
		out[count++] = cur;
		graph.for_each_neighbor(cur, [&](eng::u16 nb, eng::u16) {
			if (indegree[nb] != 0u) {
				--indegree[nb];
				if (indegree[nb] == 0u && tail < N) {
					queue[tail++] = nb;
				}
			}
		});
	}
	return count;
}

} // namespace eng::util
