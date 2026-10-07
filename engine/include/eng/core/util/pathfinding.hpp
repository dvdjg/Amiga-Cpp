#pragma once

/// \file pathfinding.hpp
/// **Búsqueda de caminos en rejilla** (`eng::util`): BFS (sin peso) y A* (con coste)
/// sobre una malla `W×H` de cuatro vecinos, con `scratch` que aporta el llamador (sin
/// heap). Todo entero, sin `float`; `W` debe ser potencia de dos (índices por máscara).
///
/// El **tipo del índice de celda** (`Index`, por defecto `u16`) y el **tipo del coste**
/// (`Cost`, por defecto `u16`) son parámetros de plantilla: una rejilla pequeña puede usar
/// `Index = u8` (tablas `came_from` de `s8`), y el límite de celdas lo fija el entero con
/// signo del mismo ancho que `Index` (las tablas guardan `-1` = no visitado).
///
/// El llamador da la transitabilidad con un callable `walkable(idx) -> bool` y, en A*,
/// el coste `cost(from, to) -> Cost`. El resultado se lee de `came_from` con
/// `reconstruct_path`.
///
/// La heurística de A* es un **punto de extensión**: por defecto **Manhattan**
/// (`detail::ManhattanH<W,H>`), que es la **óptima para la malla de 4 vecinos** (admisible
/// y ajustada con coste ≥ 1). Para mallas con movimiento en diagonal pueden pasarse
/// `detail::ChebyshevH<W,H>` u `detail::EuclideanH<W,H>` como último argumento; ambas son
/// admisibles pero más débiles en 4 vecinos.
///
/// Uso:
///   eng::s16 came_from[W*H];
///   eng::u16 queue[W*H];
///   if (eng::util::bfs<W,H>(start, goal, [&](eng::u16 i){ return !wall[i]; }, came_from, queue)) {
///       eng::u16 path[W*H];
///       const eng::usize n = eng::util::reconstruct_path<W,H>(came_from, start, goal, path);
///   }

#include <eng/core/math/isqrt.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/core/util/intmath.hpp>
#include <eng/core/util/priority_queue.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

namespace detail {

/// Nodo del heap de A* (min-heap por `f`).
template <class Index, class Cost>
struct AStarNode {
	Index idx = 0;
	Cost f = 0;
};

/// Comparador del heap: menor `f` primero.
template <class Index, class Cost>
struct AStarCmp {
	[[nodiscard]] constexpr bool operator()(const AStarNode<Index, Cost>& a,
						const AStarNode<Index, Cost>& b) const noexcept {
		return a.f > b.f; // el de menor f tiene más prioridad
	}
};

/// `log2(W)` para W potencia de dos.
consteval int log2_pow2(u16 w) {
	int e = 0;
	while (w > 1u) {
		w = static_cast<u16>(w >> 1u);
		++e;
	}
	return e;
}

/// Heurística **Manhattan** (óptima en la malla de 4 vecinos): `|dx| + |dy|`.
template <u16 W, u16 H, class Index = u16, class Cost = u16>
struct ManhattanH {
	[[nodiscard]] constexpr Cost operator()(Index a, Index b) const noexcept {
		constexpr int kLog = log2_pow2(W);
		const int ax = static_cast<int>(a & (W - 1u));
		const int ay = static_cast<int>(a >> kLog);
		const int bx = static_cast<int>(b & (W - 1u));
		const int by = static_cast<int>(b >> kLog);
		const int dx = ax > bx ? ax - bx : bx - ax;
		const int dy = ay > by ? ay - by : by - ay;
		return static_cast<Cost>(dx + dy);
	}
};

/// Heurística **Chebyshev** (para mallas con movimiento diagonal): `max(|dx|, |dy|)`.
template <u16 W, u16 H, class Index = u16, class Cost = u16>
struct ChebyshevH {
	[[nodiscard]] constexpr Cost operator()(Index a, Index b) const noexcept {
		constexpr int kLog = log2_pow2(W);
		const int ax = static_cast<int>(a & (W - 1u));
		const int ay = static_cast<int>(a >> kLog);
		const int bx = static_cast<int>(b & (W - 1u));
		const int by = static_cast<int>(b >> kLog);
		const int dx = ax > bx ? ax - bx : bx - ax;
		const int dy = ay > by ? ay - by : by - ay;
		return static_cast<Cost>(dx > dy ? dx : dy);
	}
};

/// Heurística **euclídea** (`isqrt`, admisible en 4 y 8 vecinos; más débil que Manhattan
/// en 4 vecinos).
template <u16 W, u16 H, class Index = u16, class Cost = u16>
struct EuclideanH {
	[[nodiscard]] constexpr Cost operator()(Index a, Index b) const noexcept {
		constexpr int kLog = log2_pow2(W);
		const int ax = static_cast<int>(a & (W - 1u));
		const int ay = static_cast<int>(a >> kLog);
		const int bx = static_cast<int>(b & (W - 1u));
		const int by = static_cast<int>(b >> kLog);
		const int dx = ax > bx ? ax - bx : bx - ax;
		const int dy = ay > by ? ay - by : by - ay;
		return static_cast<Cost>(eng::isqrt(static_cast<u32>(dx * dx + dy * dy)));
	}
};

} // namespace detail

/// BFS sobre la malla `W×H` (cuatro vecinos). Rellena `came_from` (índice anterior;
/// `start` apunta a sí mismo; `-1` = no visitado). Devuelve `true` si alcanzó `goal`.
template <u16 W, u16 H, class Index = u16, class Walkable>
constexpr bool bfs(type_identity_t<Index> start, type_identity_t<Index> goal, Walkable walkable,
		   Span<make_signed_t<Index>> came_from, Span<type_identity_t<Index>> queue) {
	static_assert(has_single_bit(W), "bfs: W potencia de dos");
	static_assert(static_cast<u32>(W) * H <= signed_max<Index>,
		      "bfs: la rejilla debe caber en el predecesor con signo de Index");
	constexpr usize N = static_cast<usize>(W) * H;
	constexpr int kLog = detail::log2_pow2(W);
	if (came_from.size() < N || queue.size() < N) {
		return false;
	}
	if (start >= N || goal >= N) {
		return false;
	}
	for (usize i = 0; i < N; ++i) {
		came_from[i] = -1;
	}
	usize head = 0u;
	usize tail = 0u;
	auto try_push = [&](Index from, Index to) {
		if (walkable(to) && came_from[to] == -1) {
			came_from[to] = static_cast<make_signed_t<Index>>(from);
			queue[tail++] = to;
		}
	};
	came_from[start] = static_cast<make_signed_t<Index>>(start);
	queue[tail++] = start;
	while (head < tail) {
		const Index cur = queue[head++];
		if (cur == goal) {
			return true;
		}
		const Index x = static_cast<Index>(cur & (W - 1u));
		const Index y = static_cast<Index>(cur >> kLog);
		if (x > 0u) {
			try_push(cur, static_cast<Index>(cur - 1u));
		}
		if (x + 1u < W) {
			try_push(cur, static_cast<Index>(cur + 1u));
		}
		if (y > 0u) {
			try_push(cur, static_cast<Index>(cur - W));
		}
		if (y + 1u < H) {
			try_push(cur, static_cast<Index>(cur + W));
		}
	}
	return false;
}

/// A* sobre la malla `W×H` con coste por arista. La heurística es el último parámetro
/// (por defecto `detail::ManhattanH<W,H>`, la óptima en 4 vecinos); para mallas con
/// diagonal puede pasarse `detail::ChebyshevH<W,H>` o `detail::EuclideanH<W,H>`.
template <u16 W, u16 H, class Index = u16, class Cost = u16, class Walkable, class CostFn,
	  class Heuristic = detail::ManhattanH<W, H, Index, Cost>>
constexpr bool astar(type_identity_t<Index> start, type_identity_t<Index> goal, Walkable walkable,
		     CostFn cost, Span<make_signed_t<Index>> came_from,
		     Span<type_identity_t<Cost>> g_score, Span<u8> closed,
		     Heuristic heuristic = {}) {
	static_assert(has_single_bit(W), "astar: W potencia de dos");
	static_assert(static_cast<u32>(W) * H <= signed_max<Index>,
		      "astar: la rejilla debe caber en el predecesor con signo de Index");
	static_assert(sizeof(Cost) <= 2u, "astar: Cost de 16 bits o menos (sat_add)");
	constexpr usize N = static_cast<usize>(W) * H;
	constexpr int kLog = detail::log2_pow2(W);
	constexpr Cost kInf = ~Cost {0}; // inalcanzable
	if (came_from.size() < N || g_score.size() < N || closed.size() < N) {
		return false;
	}
	if (start >= N || goal >= N) {
		return false;
	}
	for (usize i = 0; i < N; ++i) {
		came_from[i] = -1;
		g_score[i] = kInf;
		closed[i] = 0u;
	}

	PriorityQueue<detail::AStarNode<Index, Cost>, N, detail::AStarCmp<Index, Cost>> open;
	g_score[start] = 0u;
	came_from[start] = static_cast<make_signed_t<Index>>(start);
	open.push(detail::AStarNode<Index, Cost> {start, heuristic(start, goal)});

	while (!open.empty()) {
		const detail::AStarNode<Index, Cost> node = open.top();
		open.pop();
		const Index cur = node.idx;
		if (cur == goal) {
			return true;
		}
		if (closed[cur] != 0u) {
			continue;
		}
		closed[cur] = 1u;

		const Index x = static_cast<Index>(cur & (W - 1u));
		const Index y = static_cast<Index>(cur >> kLog);
		for (u8 dir = 0; dir < 4u; ++dir) {
			if ((dir == 0u && x == 0u) || (dir == 1u && x + 1u >= W) ||
			    (dir == 2u && y == 0u) || (dir == 3u && y + 1u >= H)) {
				continue;
			}
			const Index next = (dir == 0u)   ? static_cast<Index>(cur - 1u)
					   : (dir == 1u) ? static_cast<Index>(cur + 1u)
					   : (dir == 2u) ? static_cast<Index>(cur - W)
							 : static_cast<Index>(cur + W);
			if (!walkable(next)) {
				continue;
			}
			const Cost tentative = sat_add(g_score[cur], cost(cur, next));
			if (tentative < g_score[next]) {
				g_score[next] = tentative;
				came_from[next] = static_cast<make_signed_t<Index>>(cur);
				open.push(detail::AStarNode<Index, Cost> {
					next, sat_add(tentative, heuristic(next, goal))});
			}
		}
	}
	return false;
}

/// Reconstruye el camino de `start` a `goal` a partir de `came_from` (como lo deja
/// `bfs`/`astar`). Devuelve la longitud y lo escribe en `out` (de `start` a `goal`), o
/// `0` si no hay camino o no cabe.
template <u16 W, u16 H, class Index = u16>
constexpr usize reconstruct_path(Span<const make_signed_t<Index>> came_from,
				 type_identity_t<Index> start, type_identity_t<Index> goal,
				 Span<type_identity_t<Index>> out) {
	constexpr usize N = static_cast<usize>(W) * H;
	if (came_from.size() < N || start >= N || goal >= N) {
		return 0u;
	}
	if (came_from[goal] == -1 && goal != start) {
		return 0u;
	}
	usize len = 0u;
	Index cur = goal;
	for (;;) {
		if (len >= out.size()) {
			return 0u;
		}
		out[len++] = cur;
		if (cur == start) {
			break;
		}
		const make_signed_t<Index> prev = came_from[cur];
		if (prev < 0) {
			return 0u;
		}
		cur = static_cast<Index>(prev);
	}
	for (usize i = 0u; i < len / 2u; ++i) {
		const Index tmp = out[i];
		out[i] = out[len - 1u - i];
		out[len - 1u - i] = tmp;
	}
	return len;
}

} // namespace eng::util
