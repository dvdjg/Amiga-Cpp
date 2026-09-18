#pragma once

/// \file waypoints.hpp
/// **Navegación por grafo de waypoints** (`eng::ai::navigation`): nodos con posición y
/// aristas bidireccionales con coste, y búsqueda A* sobre el grafo. Complementa al
/// campo de flujo (pensado para muchos agentes hacia un mismo destino): aquí el destino
/// cambia por agente y el camino se calcula a petición.
///
/// La heurística es **Manhattan** entre posiciones de los nodos (admisible si el coste de
/// cada arista es >= su distancia Manhattan). El `scratch` lo aporta el llamador (sin
/// heap), como en `eng::util::pathfinding`.
///
/// Uso:
///   eng::ai::WaypointGraph<16, 24> graph;
///   const eng::u16 a = graph.add_node({0, 0});
///   const eng::u16 b = graph.add_node({10, 0});
///   graph.add_edge(a, b, 10);
///   eng::u16 g[16]; eng::s16 came[16]; eng::u8 closed[16]; eng::u16 path[16];
///   const eng::usize n = graph.find_path(a, b, g, came, closed, path);
///
/// Verificación: HOST-116.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/priority_queue.hpp>

namespace eng::ai {

namespace detail {

struct WpNode {
	eng::u16 idx;
	eng::u16 f;
};

struct WpCmp {
	[[nodiscard]] constexpr bool operator()(const WpNode& a, const WpNode& b) const noexcept {
		if (a.f != b.f) {
			return a.f > b.f; // min-heap por f
		}
		return a.idx > b.idx; // desempate determinista
	}
};

} // namespace detail

template <eng::u16 MaxNodes, eng::u16 MaxEdges>
class WaypointGraph {
	static_assert(MaxNodes > 0u, "WaypointGraph: MaxNodes debe ser mayor que 0");
	static_assert(MaxEdges > 0u, "WaypointGraph: MaxEdges debe ser mayor que 0");

public:
	static constexpr eng::u16 no_node = 0xffffu;

	[[nodiscard]] constexpr eng::u16 node_count() const noexcept { return m_node_count; }
	[[nodiscard]] constexpr eng::u16 edge_count() const noexcept { return m_edge_count; }
	[[nodiscard]] constexpr eng::Point2s position(eng::u16 node) const noexcept {
		return m_pos[node];
	}

	constexpr void clear() noexcept {
		m_node_count = 0u;
		m_edge_count = 0u;
	}

	/// Añade un nodo; devuelve su índice o `no_node` si no cabe.
	[[nodiscard]] constexpr eng::u16 add_node(eng::Point2s p) noexcept {
		if (m_node_count >= MaxNodes) {
			return no_node;
		}
		m_pos[m_node_count] = p;
		return m_node_count++;
	}

	/// Añade una arista **bidireccional** `a <-> b` con coste; `false` si no cabe o el
	/// nodo no existe.
	[[nodiscard]] constexpr bool add_edge(eng::u16 a, eng::u16 b, eng::u16 cost) noexcept {
		if (a >= m_node_count || b >= m_node_count || a == b || m_edge_count >= MaxEdges) {
			return false;
		}
		m_edges[m_edge_count] = Edge {a, b, cost};
		++m_edge_count;
		return true;
	}

	/// A* de `start` a `goal`. Rellena `g_score`, `came_from` (`start` apunta a sí mismo;
	/// `-1` = no visitado) y `closed`, y escribe el camino `start..goal` en `out`.
	/// Devuelve la longitud (>= 1), o `0` si no hay camino / no cabe.
	[[nodiscard]] constexpr eng::usize find_path(eng::u16 start, eng::u16 goal,
						     eng::Span<eng::u16> g_score,
						     eng::Span<eng::s16> came_from,
						     eng::Span<eng::u8> closed,
						     eng::Span<eng::u16> out) const noexcept {
		constexpr eng::usize N = MaxNodes;
		if (g_score.size() < N || came_from.size() < N || closed.size() < N) {
			return 0u;
		}
		if (start >= m_node_count || goal >= m_node_count) {
			return 0u;
		}
		for (eng::usize i = 0; i < N; ++i) {
			g_score[i] = 0xffffu;
			came_from[i] = -1;
			closed[i] = 0u;
		}

		eng::util::PriorityQueue<detail::WpNode, N, detail::WpCmp> open;
		g_score[start] = 0u;
		came_from[start] = static_cast<eng::s16>(start);
		open.push(detail::WpNode {start, heuristic(start, goal)});

		while (!open.empty()) {
			const detail::WpNode cur = open.top();
			open.pop();
			if (closed[cur.idx] != 0u) {
				continue;
			}
			closed[cur.idx] = 1u;
			if (cur.idx == goal) {
				return reconstruct(start, goal, came_from, out);
			}
			for (eng::usize e = 0; e < m_edge_count; ++e) {
				const Edge& edge = m_edges[e];
				eng::u16 nb;
				if (edge.a == cur.idx) {
					nb = edge.b;
				} else if (edge.b == cur.idx) {
					nb = edge.a;
				} else {
					continue;
				}
				if (closed[nb] != 0u) {
					continue;
				}
				const eng::u32 ng = static_cast<eng::u32>(g_score[cur.idx]) + edge.cost;
				if (ng < g_score[nb]) {
					g_score[nb] = static_cast<eng::u16>(ng > 0xffffu ? 0xffffu : ng);
					came_from[nb] = static_cast<eng::s16>(cur.idx);
					const eng::u32 f = ng + heuristic(nb, goal);
					open.push(detail::WpNode {
						nb, static_cast<eng::u16>(f > 0xffffu ? 0xffffu : f)});
				}
			}
		}
		return 0u;
	}

private:
	struct Edge {
		eng::u16 a;
		eng::u16 b;
		eng::u16 cost;
	};

	[[nodiscard]] eng::u16 heuristic(eng::u16 from, eng::u16 to) const noexcept {
		const eng::Point2s a = m_pos[from];
		const eng::Point2s b = m_pos[to];
		eng::s32 dx = static_cast<eng::s32>(a.x) - b.x;
		if (dx < 0) {
			dx = -dx;
		}
		eng::s32 dy = static_cast<eng::s32>(a.y) - b.y;
		if (dy < 0) {
			dy = -dy;
		}
		const eng::s32 d = dx + dy;
		return d > 0xffff ? static_cast<eng::u16>(0xffffu) : static_cast<eng::u16>(d);
	}

	[[nodiscard]] static eng::usize reconstruct(eng::u16 start, eng::u16 goal,
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

	eng::Point2s m_pos[MaxNodes] {};
	Edge m_edges[MaxEdges] {};
	eng::u16 m_node_count = 0u;
	eng::u16 m_edge_count = 0u;
};

} // namespace eng::ai
