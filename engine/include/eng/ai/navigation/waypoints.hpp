#pragma once

/// \file waypoints.hpp
/// **Navegación por grafo de waypoints** (`eng::ai::navigation`): nodos con posición y
/// aristas bidireccionales con coste, y búsqueda A*. Por dentro se apoya en el **grafo
/// genérico** `eng::util::Graph` (que aporta la adyacencia y el A*); este tipo añade las
/// **posiciones** y la heurística Manhattan. Complementa al campo de flujo (para muchos
/// agentes hacia un mismo destino): aquí el destino cambia por agente.
///
/// El `scratch` lo aporta el llamador (sin heap), como en `eng::util::pathfinding`.
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
#include <eng/core/util/graph.hpp>

namespace eng::ai {

template <eng::u16 MaxNodes, eng::u16 MaxEdges>
class WaypointGraph {
	static_assert(MaxNodes > 0u, "WaypointGraph: MaxNodes debe ser mayor que 0");
	static_assert(MaxEdges > 0u, "WaypointGraph: MaxEdges debe ser mayor que 0");

public:
	static constexpr eng::u16 no_node = eng::util::Graph<MaxNodes, MaxEdges>::no_node;

	[[nodiscard]] constexpr eng::u16 node_count() const noexcept { return m_graph.node_count(); }
	/// Número de aristas **lógicas** añadidas (cada una ocupa dos dirigidas en el grafo).
	[[nodiscard]] constexpr eng::u16 edge_count() const noexcept { return m_edges; }
	[[nodiscard]] constexpr eng::Point2s position(eng::u16 node) const noexcept {
		return m_pos[node];
	}

	constexpr void clear() noexcept {
		m_graph.clear();
		m_edges = 0u;
	}

	/// Añade un nodo; devuelve su índice o `no_node` si no cabe.
	[[nodiscard]] constexpr eng::u16 add_node(eng::Point2s p) noexcept {
		const eng::u16 id = m_graph.add_node();
		if (id != no_node) {
			m_pos[id] = p;
		}
		return id;
	}

	/// Añade una arista **bidireccional** `a <-> b` con coste; `false` si no cabe o el
	/// nodo no existe.
	[[nodiscard]] constexpr bool add_edge(eng::u16 a, eng::u16 b, eng::u16 cost) noexcept {
		if (!m_graph.add_edge(a, b, cost, true)) {
			return false;
		}
		++m_edges;
		return true;
	}

	/// A* de `start` a `goal`. Rellena `g_score`, `came_from` y `closed`, y escribe el
	/// camino `start..goal` en `out`. Devuelve la longitud (>= 1), o `0` si no hay camino
	/// o no cabe.
	[[nodiscard]] constexpr eng::usize find_path(eng::u16 start, eng::u16 goal,
						     eng::Span<eng::u16> g_score,
						     eng::Span<eng::s16> came_from,
						     eng::Span<eng::u8> closed,
						     eng::Span<eng::u16> out) const noexcept {
		return eng::util::graph_astar(
			m_graph, start, goal,
			[this](eng::u16 a, eng::u16 b) { return heuristic(a, b); }, g_score,
			came_from, closed, out);
	}

private:
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

	eng::util::Graph<MaxNodes, MaxEdges> m_graph {};
	eng::Point2s m_pos[MaxNodes] {};
	eng::u16 m_edges = 0u;
};

} // namespace eng::ai
