#pragma once

/// \file graph.hpp
/// `eng::util::Graph<MaxNodes, MaxEdges, Index, Cost>`: **grafo dirigido o
/// bidireccional** con nodos identificados por un entero `Index` (por defecto `u16`) y
/// aristas con coste `Cost` (por defecto `u16`), en listas de adyacencia de capacidad
/// fija (sin heap). Sobre él, algoritmos genéricos: **BFS**, **A\*** (heurística del
/// llamador) y **orden topológico** (Kahn).
///
/// Es la pieza que unifica navegación (grafos de waypoints), dependencias de assets y
/// árboles de tecnología: el dominio aporta posiciones/etiquetas y la heurística, y el
/// grafo la conectividad y la búsqueda. `Index` y `Cost` son parámetros: un grafo de
/// 8 bits (`Graph<16, 24, u8, u8>`) no arrastra `u16`; las tablas de predecesores
/// (`came_from`) usan el entero con signo del mismo ancho (`make_signed_t<Index>`).
///
/// Uso:
///   eng::util::Graph<16, 24> g;
///   const eng::u16 a = g.add_node();
///   const eng::u16 b = g.add_node();
///   g.add_edge(a, b, 10);              // bidireccional
///   g.for_each_neighbor(a, [](eng::u16 nb, eng::u16 cost) { ... });
///
/// Verificación: HOST-126.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/priority_queue.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <eng::u16 MaxNodes, eng::u16 MaxEdges, class Index = eng::u16, class Cost = eng::u16>
class Graph {
	static_assert(MaxNodes > 0u, "Graph: MaxNodes debe ser mayor que 0");
	static_assert(MaxEdges > 0u, "Graph: MaxEdges debe ser mayor que 0");
	static_assert(is_unsigned_v<Index>, "Graph: Index debe ser un entero sin signo");
	/// Máximo de `Index` como `u32` (el complemento se reduce a `Index` para no depender
	/// de la promoción de enteros pequeños).
	static constexpr eng::u32 index_max = static_cast<eng::u32>(static_cast<Index>(~Index {0}));
	static_assert(MaxNodes <= index_max, "Graph: MaxNodes debe caber en Index");
	static_assert(MaxEdges <= index_max, "Graph: MaxEdges debe caber en Index");
	static_assert(sizeof(Cost) <= 2u, "Graph: Cost de 16 bits o menos (sat_add)");

public:
	/// Tipo del identificador de nodo (el que devuelven `add_node`/`node_count`).
	using index_t = Index;
	/// Tipo del coste de arista.
	using cost_t = Cost;
	/// Centinela "no hay nodo/arista" (todos los bits de `Index` a 1).
	static constexpr Index no_node = static_cast<Index>(~Index {0});

	[[nodiscard]] constexpr Index node_count() const noexcept { return m_nodes; }
	[[nodiscard]] constexpr Index edge_count() const noexcept { return m_edge_count; }

	constexpr void clear() noexcept {
		m_nodes = 0u;
		m_edge_count = 0u;
		for (eng::u16 i = 0u; i < MaxNodes; ++i) {
			m_head[i] = no_node;
		}
	}

	/// Añade un nodo aislado; devuelve su índice o `no_node` si no cabe.
	[[nodiscard]] constexpr Index add_node() noexcept {
		if (m_nodes >= MaxNodes) {
			return no_node;
		}
		m_head[m_nodes] = no_node;
		return m_nodes++;
	}

	/// Añade una arista `a -> b` (y `b -> a` si `bidirectional`). `false` si no cabe o
	/// los nodos no existen.
	[[nodiscard]] constexpr bool add_edge(Index a, Index b, Cost cost,
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
	constexpr void for_each_neighbor(Index node, Fn fn) const {
		if (node >= m_nodes) {
			return;
		}
		for (Index e = m_head[node]; e != no_node; e = m_edges[e].next) {
			fn(m_edges[e].to, m_edges[e].cost);
		}
	}

	/// Nº de vecinos de `node` (grado de salida). Sin callback.
	[[nodiscard]] constexpr Index neighbor_count(Index node) const noexcept {
		if (node >= m_nodes) {
			return 0u;
		}
		Index n = 0u;
		for (Index e = m_head[node]; e != no_node; e = m_edges[e].next) {
			++n;
		}
		return n;
	}

	/// `i`-ésimo vecino de `node` (orden de la lista). `to`/`cost` por referencia; `false` si `i`
	/// está fuera de rango. Alternativa a `for_each_neighbor` **sin callback**: útil donde una
	/// lambda no es viable (p. ej. el GCC m68k a `-O0` da un ICE de CFI con lambdas) y más barato
	/// (indexado directo). `i` se recorre con `next` desde la cabeza.
	[[nodiscard]] constexpr bool neighbor_at(Index node, Index i, Index& to,
						 Cost& cost) const noexcept {
		if (node >= m_nodes) {
			return false;
		}
		Index e = m_head[node];
		for (Index k = 0u; e != no_node && k < i; ++k) {
			e = m_edges[e].next;
		}
		if (e == no_node) {
			return false;
		}
		to = m_edges[e].to;
		cost = m_edges[e].cost;
		return true;
	}

private:
	struct Edge {
		Index to;
		Cost cost;
		Index next;
	};

	[[nodiscard]] constexpr bool push_edge(Index from, Index to, Cost cost) noexcept {
		if (m_edge_count >= MaxEdges) {
			return false;
		}
		m_edges[m_edge_count] = Edge {to, cost, m_head[from]};
		m_head[from] = m_edge_count;
		++m_edge_count;
		return true;
	}

	Edge m_edges[MaxEdges] {};
	Index m_head[MaxNodes] {};
	Index m_nodes = 0u;
	Index m_edge_count = 0u;
};

namespace detail {

/// Nodo del heap de A* (min-heap por `f`).
template <class Index, class Cost>
struct GraphNode {
	Index idx;
	Cost f;
};

/// Comparador del heap: menor `f` primero; desempata por índice (determinista).
template <class Index, class Cost>
struct GraphCmp {
	[[nodiscard]] constexpr bool operator()(const GraphNode<Index, Cost>& a,
						const GraphNode<Index, Cost>& b) const noexcept {
		if (a.f != b.f) {
			return a.f > b.f;
		}
		return a.idx > b.idx;
	}
};

/// Reconstruye `start..goal` desde `came_from` (como lo dejan `graph_bfs`/`graph_astar`).
template <class Index>
[[nodiscard]] constexpr eng::usize graph_reconstruct(Index start, Index goal,
						     eng::Span<make_signed_t<Index>> came_from,
						     eng::Span<Index> out) noexcept {
	eng::usize count = 0u;
	Index n = goal;
	for (;;) {
		if (count >= out.size()) {
			return 0u;
		}
		out[count++] = n;
		if (n == start) {
			break;
		}
		const make_signed_t<Index> prev = came_from[n];
		if (prev < 0) {
			return 0u;
		}
		n = static_cast<Index>(prev);
	}
	for (eng::usize i = 0u; i < count / 2u; ++i) {
		const Index tmp = out[i];
		out[i] = out[count - 1u - i];
		out[count - 1u - i] = tmp;
	}
	return count;
}

} // namespace detail

/// BFS sin peso en `graph` de `start` a `goal`. `queue` es scratch del llamador
/// (`MaxNodes`). Rellena `came_from` (`start` -> sí mismo; `-1` = no visitado) y el
/// camino en `out`. Devuelve la longitud, o `0`.
template <eng::u16 MaxNodes, eng::u16 MaxEdges, class Index, class Cost>
[[nodiscard]] constexpr eng::usize graph_bfs(const Graph<MaxNodes, MaxEdges, Index, Cost>& graph,
					     type_identity_t<Index> start, type_identity_t<Index> goal,
					     eng::Span<make_signed_t<Index>> came_from,
					     eng::Span<type_identity_t<Index>> queue,
					     eng::Span<type_identity_t<Index>> out) noexcept {
	static_assert(MaxNodes <= signed_max<Index>,
		      "graph_bfs: MaxNodes debe caber en el predecesor con signo");
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
	came_from[start] = static_cast<make_signed_t<Index>>(start);
	queue[tail++] = start;
	while (head < tail) {
		const Index cur = queue[head++];
		if (cur == goal) {
			return detail::graph_reconstruct(start, goal, came_from, out);
		}
		graph.for_each_neighbor(cur, [&](Index nb, Cost) {
			if (came_from[nb] == -1 && tail < N) {
				came_from[nb] = static_cast<make_signed_t<Index>>(cur);
				queue[tail++] = nb;
			}
		});
	}
	return 0u;
}

/// A* en `graph` de `start` a `goal` con coste por arista y heurística del llamador
/// (`heuristic(node, goal) -> Cost`, admisible). Mismo `scratch` que `eng::util::astar`.
template <eng::u16 MaxNodes, eng::u16 MaxEdges, class Index, class Cost, class Heuristic>
[[nodiscard]] constexpr eng::usize graph_astar(const Graph<MaxNodes, MaxEdges, Index, Cost>& graph,
					       type_identity_t<Index> start, type_identity_t<Index> goal,
					       Heuristic heuristic,
					       eng::Span<type_identity_t<Cost>> g_score,
					       eng::Span<make_signed_t<Index>> came_from,
					       eng::Span<eng::u8> closed,
					       eng::Span<type_identity_t<Index>> out) noexcept {
	static_assert(MaxNodes <= signed_max<Index>,
		      "graph_astar: MaxNodes debe caber en el predecesor con signo");
	constexpr eng::usize N = MaxNodes;
	constexpr Cost kInf = ~Cost {0}; // inalcanzable
	if (g_score.size() < N || came_from.size() < N || closed.size() < N || out.empty()) {
		return 0u;
	}
	if (start >= graph.node_count() || goal >= graph.node_count()) {
		return 0u;
	}
	for (eng::usize i = 0; i < N; ++i) {
		g_score[i] = kInf;
		came_from[i] = -1;
		closed[i] = 0u;
	}

	PriorityQueue<detail::GraphNode<Index, Cost>, N, detail::GraphCmp<Index, Cost>> open;
	g_score[start] = 0u;
	came_from[start] = static_cast<make_signed_t<Index>>(start);
	open.push(detail::GraphNode<Index, Cost> {start, heuristic(start, goal)});

	while (!open.empty()) {
		const detail::GraphNode<Index, Cost> cur = open.top();
		open.pop();
		if (closed[cur.idx] != 0u) {
			continue;
		}
		closed[cur.idx] = 1u;
		if (cur.idx == goal) {
			return detail::graph_reconstruct(start, goal, came_from, out);
		}
		graph.for_each_neighbor(cur.idx, [&](Index nb, Cost cost) {
			if (closed[nb] != 0u) {
				return;
			}
			const Cost ng = sat_add(g_score[cur.idx], cost);
			if (ng < g_score[nb]) {
				g_score[nb] = ng;
				came_from[nb] = static_cast<make_signed_t<Index>>(cur.idx);
				open.push(detail::GraphNode<Index, Cost> {nb, sat_add(ng, heuristic(nb, goal))});
			}
		});
	}
	return 0u;
}

/// Orden topológico (Kahn) de un grafo dirigido acíclico. `indegree` y `queue` son
/// scratch (`MaxNodes`). Escribe los nodos en `out` y devuelve cuántos; si es menor que
/// `node_count()`, hay un ciclo.
template <eng::u16 MaxNodes, eng::u16 MaxEdges, class Index, class Cost>
[[nodiscard]] constexpr eng::usize topological_sort(const Graph<MaxNodes, MaxEdges, Index, Cost>& graph,
						    eng::Span<type_identity_t<Index>> indegree,
						    eng::Span<type_identity_t<Index>> queue,
						    eng::Span<type_identity_t<Index>> out) noexcept {
	constexpr eng::usize N = MaxNodes;
	if (indegree.size() < N || queue.size() < N) {
		return 0u;
	}
	for (eng::usize i = 0; i < N; ++i) {
		indegree[i] = 0u;
	}
	for (Index n = 0u; n < graph.node_count(); ++n) {
		graph.for_each_neighbor(n, [&](Index nb, Cost) {
			++indegree[nb];
		});
	}
	eng::usize head = 0u;
	eng::usize tail = 0u;
	for (Index n = 0u; n < graph.node_count(); ++n) {
		if (indegree[n] == 0u) {
			queue[tail++] = n;
		}
	}
	eng::usize count = 0u;
	while (head < tail) {
		const Index cur = queue[head++];
		if (count >= out.size()) {
			return 0u;
		}
		out[count++] = cur;
		graph.for_each_neighbor(cur, [&](Index nb, Cost) {
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
