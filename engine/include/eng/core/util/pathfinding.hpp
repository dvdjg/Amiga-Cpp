#pragma once

/// \file pathfinding.hpp
/// **Búsqueda de caminos en rejilla** (`eng::util`): BFS (sin peso) y A* (con coste)
/// sobre una malla `W×H` de cuatro vecinos, con `scratch` que aporta el llamador (sin
/// heap). Todo entero, sin `float`; `W` debe ser potencia de dos (índices por máscara).
///
/// El llamador da la transitabilidad con un callable `walkable(idx) -> bool` y, en A*,
/// el coste `cost(from, to) -> u16`. El resultado se lee de `came_from` con
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
#include <eng/core/util/priority_queue.hpp>

namespace eng::util {

namespace detail {

/// Nodo del heap de A* (min-heap por `f`).
struct AStarNode {
	u16 idx = 0;
	u16 f = 0;
};
struct AStarCmp {
	[[nodiscard]] constexpr bool operator()(const AStarNode& a, const AStarNode& b) const noexcept {
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
template <u16 W, u16 H>
struct ManhattanH {
	[[nodiscard]] constexpr u16 operator()(u16 a, u16 b) const noexcept {
		constexpr int kLog = log2_pow2(W);
		const int ax = static_cast<int>(a & (W - 1u));
		const int ay = static_cast<int>(a >> kLog);
		const int bx = static_cast<int>(b & (W - 1u));
		const int by = static_cast<int>(b >> kLog);
		const int dx = ax > bx ? ax - bx : bx - ax;
		const int dy = ay > by ? ay - by : by - ay;
		return static_cast<u16>(dx + dy);
	}
};

/// Heurística **Chebyshev** (para mallas con movimiento diagonal): `max(|dx|, |dy|)`.
template <u16 W, u16 H>
struct ChebyshevH {
	[[nodiscard]] constexpr u16 operator()(u16 a, u16 b) const noexcept {
		constexpr int kLog = log2_pow2(W);
		const int ax = static_cast<int>(a & (W - 1u));
		const int ay = static_cast<int>(a >> kLog);
		const int bx = static_cast<int>(b & (W - 1u));
		const int by = static_cast<int>(b >> kLog);
		const int dx = ax > bx ? ax - bx : bx - ax;
		const int dy = ay > by ? ay - by : by - ay;
		return static_cast<u16>(dx > dy ? dx : dy);
	}
};

/// Heurística **euclídea** (`isqrt`, admisible en 4 y 8 vecinos; más débil que Manhattan
/// en 4 vecinos).
template <u16 W, u16 H>
struct EuclideanH {
	[[nodiscard]] constexpr u16 operator()(u16 a, u16 b) const noexcept {
		constexpr int kLog = log2_pow2(W);
		const int ax = static_cast<int>(a & (W - 1u));
		const int ay = static_cast<int>(a >> kLog);
		const int bx = static_cast<int>(b & (W - 1u));
		const int by = static_cast<int>(b >> kLog);
		const int dx = ax > bx ? ax - bx : bx - ax;
		const int dy = ay > by ? ay - by : by - ay;
		return static_cast<u16>(eng::isqrt(static_cast<u32>(dx * dx + dy * dy)));
	}
};

} // namespace detail


/// BFS sobre la malla `W×H` (cuatro vecinos). Rellena `came_from` (índice anterior;
/// `start` apunta a sí mismo; `-1` = no visitado). Devuelve `true` si alcanzó `goal`.
template <u16 W, u16 H, class Walkable>
constexpr bool bfs(u16 start, u16 goal, Walkable walkable, Span<s16> came_from,
		   Span<u16> queue) {
	static_assert(has_single_bit(W), "bfs: W potencia de dos");
	static_assert(static_cast<u32>(W) * H <= 32767u, "bfs: la rejilla debe caber en s16");
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
	auto try_push = [&](u16 from, u16 to) {
		if (walkable(to) && came_from[to] == -1) {
			came_from[to] = static_cast<s16>(from);
			queue[tail++] = to;
		}
	};
	came_from[start] = static_cast<s16>(start);
	queue[tail++] = start;
	while (head < tail) {
		const u16 cur = queue[head++];
		if (cur == goal) {
			return true;
		}
		const u16 x = static_cast<u16>(cur & (W - 1u));
		const u16 y = static_cast<u16>(cur >> kLog);
		if (x > 0u) {
			try_push(cur, static_cast<u16>(cur - 1u));
		}
		if (x + 1u < W) {
			try_push(cur, static_cast<u16>(cur + 1u));
		}
		if (y > 0u) {
			try_push(cur, static_cast<u16>(cur - W));
		}
		if (y + 1u < H) {
			try_push(cur, static_cast<u16>(cur + W));
		}
	}
	return false;
}

/// A* sobre la malla `W×H` con coste por arista. La heurística es el último parámetro
/// (por defecto `detail::ManhattanH<W,H>`, la óptima en 4 vecinos); para mallas con
/// diagonal puede pasarse `detail::ChebyshevH<W,H>` o `detail::EuclideanH<W,H>`.
template <u16 W, u16 H, class Walkable, class Cost,
	  class Heuristic = detail::ManhattanH<W, H>>
constexpr bool astar(u16 start, u16 goal, Walkable walkable, Cost cost, Span<s16> came_from,
		     Span<u16> g_score, Span<u8> closed, Heuristic heuristic = {}) {
	static_assert(has_single_bit(W), "astar: W potencia de dos");
	static_assert(static_cast<u32>(W) * H <= 32767u, "astar: la rejilla debe caber en s16");
	constexpr usize N = static_cast<usize>(W) * H;
	constexpr int kLog = detail::log2_pow2(W);
	if (came_from.size() < N || g_score.size() < N || closed.size() < N) {
		return false;
	}
	if (start >= N || goal >= N) {
		return false;
	}
	for (usize i = 0; i < N; ++i) {
		came_from[i] = -1;
		g_score[i] = 0xffffu;
		closed[i] = 0u;
	}

	PriorityQueue<detail::AStarNode, N, detail::AStarCmp> open;
	g_score[start] = 0u;
	came_from[start] = static_cast<s16>(start);
	open.push(detail::AStarNode {start, heuristic(start, goal)});

	while (!open.empty()) {
		const detail::AStarNode node = open.top();
		open.pop();
		const u16 cur = node.idx;
		if (cur == goal) {
			return true;
		}
		if (closed[cur] != 0u) {
			continue;
		}
		closed[cur] = 1u;

		const u16 x = static_cast<u16>(cur & (W - 1u));
		const u16 y = static_cast<u16>(cur >> kLog);
		for (u8 dir = 0; dir < 4u; ++dir) {
			if ((dir == 0u && x == 0u) || (dir == 1u && x + 1u >= W) ||
			    (dir == 2u && y == 0u) || (dir == 3u && y + 1u >= H)) {
				continue;
			}
			const u16 next = (dir == 0u)   ? static_cast<u16>(cur - 1u)
					 : (dir == 1u) ? static_cast<u16>(cur + 1u)
					 : (dir == 2u) ? static_cast<u16>(cur - W)
						       : static_cast<u16>(cur + W);
			if (!walkable(next)) {
				continue;
			}
			const u16 tentative = static_cast<u16>(g_score[cur] + cost(cur, next));
			if (tentative < g_score[next]) {
				g_score[next] = tentative;
				came_from[next] = static_cast<s16>(cur);
				open.push(detail::AStarNode {
					next, static_cast<u16>(tentative + heuristic(next, goal))});
			}
		}
	}
	return false;
}

/// Reconstruye el camino de `start` a `goal` a partir de `came_from` (como lo deja
/// `bfs`/`astar`). Devuelve la longitud y lo escribe en `out` (de `start` a `goal`), o
/// `0` si no hay camino o no cabe.
template <u16 W, u16 H>
constexpr usize reconstruct_path(Span<const s16> came_from, u16 start, u16 goal,
				 Span<u16> out) {
	constexpr usize N = static_cast<usize>(W) * H;
	if (came_from.size() < N || start >= N || goal >= N) {
		return 0u;
	}
	if (came_from[goal] == -1 && goal != start) {
		return 0u;
	}
	usize len = 0u;
	u16 cur = goal;
	for (;;) {
		if (len >= out.size()) {
			return 0u;
		}
		out[len++] = cur;
		if (cur == start) {
			break;
		}
		const s16 prev = came_from[cur];
		if (prev < 0) {
			return 0u;
		}
		cur = static_cast<u16>(prev);
	}
	for (usize i = 0u; i < len / 2u; ++i) {
		const u16 tmp = out[i];
		out[i] = out[len - 1u - i];
		out[len - 1u - i] = tmp;
	}
	return len;
}

} // namespace eng::util
