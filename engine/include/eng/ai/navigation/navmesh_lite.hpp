#pragma once

/// \file navmesh_lite.hpp
/// **Navmesh lite** (`eng::ai::navigation`): una versión mínima de la idea de
/// Recast/Detour (https://github.com/recastnavigation/recastnavigation) para el A500.
/// En vez de un grafo de puntos, el mundo transitables se divide en **polígonos
/// convexos**; el agente consulta en qué polígono está, A* recorre la **adyacencia entre
/// polígonos** y el camino devuelto son los **puntos medios de los portales** (la arista
/// compartida entre dos polígonos) más el destino.
///
/// La malla la aporta el juego (`add_polygon`/`add_portal`); no se genera ni se cuece.
/// Dos consultas: `find_path` (puntos medios de los portales, sin zigzag solo si el
/// pasillo es recto) y `find_smooth_path`, que aplica **string-pulling** con el algoritmo
/// del embudo (*simple stupid funnel*) y devuelve solo las esquinas visibles. Todo sin heap
/// y sin división por valores de runtime (los costes usan el **primer vértice** de cada
/// polígono como ancla, no el centroide).
///
/// Uso:
///   eng::ai::NavMesh<8, 4, 8> mesh;
///   const eng::u16 a = mesh.add_polygon({{0,0},{10,0},{10,10},{0,10}});
///   const eng::u16 b = mesh.add_polygon({{10,0},{20,0},{20,10},{10,10}});
///   mesh.add_portal(a, b, {10, 0}, {10, 10});
///   eng::Point2s path[8];
///   const eng::usize n = mesh.find_path({5,5}, {15,5}, g, came, closed, path);
///
/// ```text
///   mundo transitable              NavMesh (polígonos convexos)              agente
///   ─────────────────              ────────────────────────────              ──────
///   add_polygon(vértices) ──► [ polígonos convexos: vértices + adyacencia ]
///   add_portal(a,b,p0,p1) ──► [ portales: arista compartida a↔b ] ──► A* por adyacencia (sin heap)
///                                                                        │
///                        find_path ─────────► puntos MEDIOS de portales ─┘
///                        find_smooth_path ──► string-pulling (embudo) = solo esquinas visibles
/// ```
///
/// Verificación: HOST-118.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/priority_queue.hpp>
#include <eng/core/arith.hpp>

namespace eng::ai {

namespace detail {

struct NavNode {
	eng::u16 idx;
	eng::u16 f;
};

struct NavCmp {
	[[nodiscard]] constexpr bool operator()(const NavNode& a, const NavNode& b) const noexcept {
		if (a.f != b.f) {
			return a.f > b.f; // min-heap por f
		}
		return a.idx > b.idx; // desempate determinista
	}
};

} // namespace detail

template <eng::u16 MaxPolys, eng::u16 MaxVertsPerPoly, eng::u16 MaxPortals>
class NavMesh {
	static_assert(MaxPolys > 0u, "NavMesh: MaxPolys debe ser mayor que 0");
	static_assert(MaxVertsPerPoly >= 3u, "NavMesh: un poligono necesita 3 vertices");
	static_assert(MaxPortals > 0u, "NavMesh: MaxPortals debe ser mayor que 0");

public:
	static constexpr eng::u16 no_poly = 0xffffu;

	[[nodiscard]] constexpr eng::u16 poly_count() const noexcept { return m_poly_count; }
	[[nodiscard]] constexpr eng::u16 portal_count() const noexcept { return m_portal_count; }

	/// Añade un polígono **convexo** (3..`MaxVertsPerPoly` vértices); devuelve su índice
	/// o `no_poly` si no cabe.
	[[nodiscard]] constexpr eng::u16 add_polygon(eng::Span<const eng::Point2s> verts) noexcept {
		if (m_poly_count >= MaxPolys || verts.size() < 3u ||
		    verts.size() > MaxVertsPerPoly) {
			return no_poly;
		}
		for (eng::usize i = 0; i < verts.size(); ++i) {
			m_verts[m_poly_count][i] = verts[i];
		}
		m_counts[m_poly_count] = static_cast<eng::u16>(verts.size());
		return m_poly_count++;
	}

	/// Declara un **portal** (arista compartida) entre `a` y `b`, con sus dos extremos
	/// `p0`/`p1`. `false` si no cabe o los polígonos no existen.
	[[nodiscard]] constexpr bool add_portal(eng::u16 a, eng::u16 b, eng::Point2s p0,
						eng::Point2s p1) noexcept {
		if (a >= m_poly_count || b >= m_poly_count || a == b ||
		    m_portal_count >= MaxPortals) {
			return false;
		}
		m_portals[m_portal_count] = Portal {a, b, p0, p1};
		++m_portal_count;
		return true;
	}

	/// ¿Está `p` dentro del polígono convexo `poly`? (incluye el borde).
	[[nodiscard]] constexpr bool contains(eng::u16 poly, eng::Point2s p) const noexcept {
		const eng::u16 n = m_counts[poly];
		bool pos = false;
		bool neg = false;
		for (eng::u16 i = 0u; i < n; ++i) {
			const eng::Point2s a = m_verts[poly][i];
			const eng::Point2s b = m_verts[poly][static_cast<eng::u16>((i + 1u) % n)];
			const eng::s32 c = cross(a, b, p);
			if (c > 0) {
				pos = true;
			} else if (c < 0) {
				neg = true;
			}
			if (pos && neg) {
				return false;
			}
		}
		return true;
	}

	/// Polígono que contiene `p`, o `no_poly`.
	[[nodiscard]] constexpr eng::u16 locate(eng::Point2s p) const noexcept {
		for (eng::u16 i = 0u; i < m_poly_count; ++i) {
			if (contains(i, p)) {
				return i;
			}
		}
		return no_poly;
	}

	/// A* sobre la adyacencia de polígonos de `start` a `goal`. Rellena `g_score`,
	/// `came_from` y `closed` (por polígono), y escribe en `out` los puntos medios de los
	/// portales intermedios más el destino. Devuelve el número de puntos (>= 1), o `0` si
	/// no hay camino / algún punto cae fuera de la malla / no cabe.
	[[nodiscard]] constexpr eng::usize find_path(eng::Point2s start, eng::Point2s goal,
						     eng::Span<eng::u16> g_score,
						     eng::Span<eng::s16> came_from,
						     eng::Span<eng::u8> closed,
						     eng::Span<eng::Point2s> out) const noexcept {
		constexpr eng::usize N = MaxPolys;
		if (g_score.size() < N || came_from.size() < N || closed.size() < N ||
		    out.empty()) {
			return 0u;
		}
		const eng::u16 s = locate(start);
		const eng::u16 g = locate(goal);
		if (s == no_poly || g == no_poly) {
			return 0u;
		}
		if (s == g) {
			out[0] = goal;
			return 1u;
		}
		if (!search_polys(s, g, g_score, came_from, closed)) {
			return 0u;
		}
		return emit_path(s, g, goal, came_from, out);
	}

	/// Como `find_path`, pero aplica **string-pulling** (funnel) sobre los portales: el
	/// camino sale como la secuencia de esquinas visibles (menos puntos y sin zigzag).
	/// Mismos requisitos y valores de retorno que `find_path`.
	[[nodiscard]] constexpr eng::usize find_smooth_path(eng::Point2s start, eng::Point2s goal,
							    eng::Span<eng::u16> g_score,
							    eng::Span<eng::s16> came_from,
							    eng::Span<eng::u8> closed,
							    eng::Span<eng::Point2s> out) const noexcept {
		constexpr eng::usize N = MaxPolys;
		if (g_score.size() < N || came_from.size() < N || closed.size() < N ||
		    out.empty()) {
			return 0u;
		}
		const eng::u16 s = locate(start);
		const eng::u16 g = locate(goal);
		if (s == no_poly || g == no_poly) {
			return 0u;
		}
		if (s == g) {
			out[0] = goal;
			return 1u;
		}
		if (!search_polys(s, g, g_score, came_from, closed)) {
			return 0u;
		}
		eng::u16 seq[MaxPolys] {};
		const eng::usize len = reconstruct_seq(
			s, g, came_from, eng::Span<eng::u16> {seq, MaxPolys});
		if (len < 2u) {
			return 0u;
		}

		eng::Point2s lefts[MaxPolys] {};
		eng::Point2s rights[MaxPolys] {};
		eng::usize portals = 0u;
		for (eng::usize i = 0u; i + 1u < len; ++i) {
			const Portal* portal = find_portal(seq[i], seq[i + 1u]);
			if (portal == nullptr) {
				continue;
			}
			orient(seq[i + 1u], portal->p0, portal->p1, lefts[portals],
			       rights[portals]);
			++portals;
		}
		if (portals == 0u) {
			out[0] = goal;
			return 1u;
		}
		return funnel(start, goal, eng::Span<const eng::Point2s> {lefts, portals},
			      eng::Span<const eng::Point2s> {rights, portals}, out);
	}

private:
	struct Portal {
		eng::u16 a;
		eng::u16 b;
		eng::Point2s p0;
		eng::Point2s p1;
	};

	/// Producto vectorial `(b-a) x (p-a)` con `muls.w` (16x16 -> 32): las diferencias
	/// deben caber en `s16` (coordenadas de malla pequeñas).
	[[nodiscard]] static constexpr eng::s32 cross(eng::Point2s a, eng::Point2s b,
						      eng::Point2s p) noexcept {
		const eng::s16 abx = static_cast<eng::s16>(b.x - a.x);
		const eng::s16 aby = static_cast<eng::s16>(b.y - a.y);
		const eng::s16 apx = static_cast<eng::s16>(p.x - a.x);
		const eng::s16 apy = static_cast<eng::s16>(p.y - a.y);
		return eng::math::mul_wide(abx, apy) - eng::math::mul_wide(aby, apx);
	}

	/// Distancia Manhattan `|dx| + |dy|` (saturada a `u16`). Es **admisible** para la
	/// heurística del A* y se usa también como métrica auxiliar.
	[[nodiscard]] static constexpr eng::u16 manhattan(eng::Point2s a,
							  eng::Point2s b) noexcept {
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

	/// Ancla = primer vértice (evita dividir para el centroide).
	[[nodiscard]] constexpr eng::u16 anchor_distance(eng::u16 a, eng::u16 b) const noexcept {
		return manhattan(m_verts[a][0], m_verts[b][0]);
	}

	/// Heurística admisible del A* entre los nodos `from`/`to` (Manhattan entre sus vértices
	/// ancla). La usa el cálculo de caminos (`find_path`).
	[[nodiscard]] constexpr eng::u16 heuristic(eng::u16 from, eng::u16 to) const noexcept {
		return manhattan(m_verts[from][0], m_verts[to][0]);
	}

	/// Punto medio entre `a` y `b`. Lo usa la construcción de portales entre polígonos.
	[[nodiscard]] static constexpr eng::Point2s midpoint(eng::Point2s a,
							     eng::Point2s b) noexcept {
		return eng::Point2s {
			static_cast<eng::s16>((static_cast<eng::s32>(a.x) + b.x) / 2),
			static_cast<eng::s16>((static_cast<eng::s32>(a.y) + b.y) / 2)};
	}

	/// A* sobre la adyacencia; deja `came_from`/`g_score`/`closed` y dice si llegó a `g`.
	[[nodiscard]] constexpr bool search_polys(eng::u16 s, eng::u16 g,
						  eng::Span<eng::u16> g_score,
						  eng::Span<eng::s16> came_from,
						  eng::Span<eng::u8> closed) const noexcept {
		constexpr eng::usize N = MaxPolys;
		for (eng::usize i = 0; i < N; ++i) {
			g_score[i] = 0xffffu;
			came_from[i] = -1;
			closed[i] = 0u;
		}
		eng::util::PriorityQueue<detail::NavNode, N, detail::NavCmp> open;
		g_score[s] = 0u;
		came_from[s] = static_cast<eng::s16>(s);
		open.push(detail::NavNode {s, heuristic(s, g)});

		while (!open.empty()) {
			const detail::NavNode cur = open.top();
			open.pop();
			if (closed[cur.idx] != 0u) {
				continue;
			}
			closed[cur.idx] = 1u;
			if (cur.idx == g) {
				return true;
			}
			for (eng::u16 e = 0u; e < m_portal_count; ++e) {
				const Portal& portal = m_portals[e];
				eng::u16 nb;
				if (portal.a == cur.idx) {
					nb = portal.b;
				} else if (portal.b == cur.idx) {
					nb = portal.a;
				} else {
					continue;
				}
				if (closed[nb] != 0u) {
					continue;
				}
				const eng::u32 ng = static_cast<eng::u32>(g_score[cur.idx]) +
						    anchor_distance(cur.idx, nb);
				if (ng < g_score[nb]) {
					g_score[nb] = static_cast<eng::u16>(ng > 0xffffu ? 0xffffu : ng);
					came_from[nb] = static_cast<eng::s16>(cur.idx);
					const eng::u32 f = ng + heuristic(nb, g);
					open.push(detail::NavNode {
						nb,
						static_cast<eng::u16>(f > 0xffffu ? 0xffffu : f)});
				}
			}
		}
		return false;
	}

	/// Secuencia de polígonos `s..g` (ambos incluidos) a partir de `came_from`.
	[[nodiscard]] constexpr eng::usize reconstruct_seq(eng::u16 s, eng::u16 g,
							   eng::Span<eng::s16> came_from,
							   eng::Span<eng::u16> seq) const noexcept {
		eng::usize len = 0u;
		eng::u16 n = g;
		for (;;) {
			if (len >= seq.size()) {
				return 0u;
			}
			seq[len++] = n;
			if (n == s) {
				break;
			}
			const eng::s16 prev = came_from[n];
			if (prev < 0) {
				return 0u;
			}
			n = static_cast<eng::u16>(prev);
		}
		for (eng::usize i = 0u; i < len / 2u; ++i) {
			const eng::u16 tmp = seq[i];
			seq[i] = seq[len - 1u - i];
			seq[len - 1u - i] = tmp;
		}
		return len;
	}

	[[nodiscard]] constexpr const Portal* find_portal(eng::u16 a, eng::u16 b) const noexcept {
		for (eng::u16 e = 0u; e < m_portal_count; ++e) {
			const Portal& portal = m_portals[e];
			if ((portal.a == a && portal.b == b) ||
			    (portal.a == b && portal.b == a)) {
				return &m_portals[e];
			}
		}
		return nullptr;
	}

	[[nodiscard]] static constexpr bool eq(eng::Point2s a, eng::Point2s b) noexcept {
		return a.x == b.x && a.y == b.y;
	}

	/// Orienta los extremos del portal como izquierda/derecha del sentido de avance,
	/// usando un vértice interior del polígono vecino.
	constexpr void orient(eng::u16 interior_poly, eng::Point2s p0, eng::Point2s p1,
			      eng::Point2s& left, eng::Point2s& right) const noexcept {
		const eng::u16 n = m_counts[interior_poly];
		eng::Point2s other {};
		bool found = false;
		for (eng::u16 i = 0u; i < n; ++i) {
			const eng::Point2s v = m_verts[interior_poly][i];
			if (!eq(v, p0) && !eq(v, p1)) {
				other = v;
				found = true;
				break;
			}
		}
		if (!found) {
			left = p0;
			right = p1;
			return;
		}
		const eng::Point2s mid = midpoint(p0, p1);
		if (cross(mid, other, p0) > 0) {
			left = p0;
			right = p1;
		} else {
			left = p1;
			right = p0;
		}
	}

	/// Algoritmo del embudo (simple stupid funnel): encoge el pasillo de portales a la
	/// secuencia de esquinas visibles.
	[[nodiscard]] static constexpr eng::usize funnel(
		eng::Point2s start, eng::Point2s goal,
		eng::Span<const eng::Point2s> lefts, eng::Span<const eng::Point2s> rights,
		eng::Span<eng::Point2s> out) noexcept {
		if (out.empty()) {
			return 0u;
		}
		eng::usize count = 0u;
		out[count++] = start;
		eng::Point2s apex = start;
		eng::Point2s left = start;
		eng::Point2s right = start;
		eng::s32 apex_i = -1;
		eng::s32 left_i = -1;
		eng::s32 right_i = -1;
		const eng::s32 n = static_cast<eng::s32>(lefts.size());
		for (eng::s32 i = 0; i < n; ++i) {
			const eng::Point2s p_left = lefts[static_cast<eng::usize>(i)];
			const eng::Point2s p_right = rights[static_cast<eng::usize>(i)];
			if (cross(apex, right, p_right) <= 0) {
				if (right_i == apex_i || cross(apex, left, p_right) > 0) {
					right = p_right;
					right_i = i;
				} else {
					if (count >= out.size()) {
						return 0u;
					}
					out[count++] = left;
					apex = left;
					apex_i = left_i;
					left = apex;
					right = apex;
					left_i = right_i = apex_i;
					i = apex_i;
					continue;
				}
			}
			if (cross(apex, left, p_left) >= 0) {
				if (left_i == apex_i || cross(apex, right, p_left) < 0) {
					left = p_left;
					left_i = i;
				} else {
					if (count >= out.size()) {
						return 0u;
					}
					out[count++] = right;
					apex = right;
					apex_i = right_i;
					left = apex;
					right = apex;
					left_i = right_i = apex_i;
					i = apex_i;
					continue;
				}
			}
		}
		if (count >= out.size()) {
			return 0u;
		}
		out[count++] = goal;
		return count;
	}

	[[nodiscard]] constexpr eng::usize emit_path(eng::u16 s, eng::u16 g,
						     eng::Point2s goal,
						     eng::Span<eng::s16> came_from,
						     eng::Span<eng::Point2s> out) const noexcept {
		eng::u16 seq[MaxPolys] {};
		const eng::usize len = reconstruct_seq(
			s, g, came_from, eng::Span<eng::u16> {seq, MaxPolys});
		if (len < 1u) {
			return 0u;
		}
		eng::usize count = 0u;
		for (eng::usize i = 0u; i + 1u < len; ++i) {
			const Portal* portal = find_portal(seq[i], seq[i + 1u]);
			if (portal == nullptr) {
				continue;
			}
			if (count >= out.size()) {
				return 0u;
			}
			out[count++] = midpoint(portal->p0, portal->p1);
		}
		if (count >= out.size()) {
			return 0u;
		}
		out[count++] = goal;
		return count;
	}

	eng::Point2s m_verts[MaxPolys][MaxVertsPerPoly] {};
	eng::u16 m_counts[MaxPolys] {};
	Portal m_portals[MaxPortals] {};
	eng::u16 m_poly_count = 0u;
	eng::u16 m_portal_count = 0u;
};

} // namespace eng::ai
