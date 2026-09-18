#pragma once

/// \file collision.hpp
/// **Pruebas de colisión 2D** enteras (`eng::util`), pensadas para el 68000: sin
/// `float`, sin división y con productos de 16×16 (`muls.w`, `arith.hpp`).
///
/// Coordenadas en `s16` (`Point2s`) y diferencias que también deben caber en `s16`
/// (espacio de pantalla/tile, ~±16000): así los productos cruzados y las distancias
/// al cuadrado caben en `s32` sin promoción a 64 bits (que sería `__muldi3`).
///
/// Uso:
///   const eng::Aabb box {{0, 0, 31, 31}};
///   if (eng::util::aabb_overlap(box, other)) { ... }
///   if (eng::util::segments_intersect(a, b, c, d)) { ... }

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/arith.hpp>

namespace eng::util {

/// Caja alineada a los ejes (bordes exclusivos: `[min,max)`).
struct Aabb {
	s16 min_x = 0;
	s16 min_y = 0;
	s16 max_x = 0;
	s16 max_y = 0;
};

/// ¿Se solapan dos cajas? (bordes exclusivos; tocar por el borde no cuenta).
[[nodiscard]] constexpr bool aabb_overlap(const Aabb& a, const Aabb& b) noexcept {
	if (a.max_x <= b.min_x || b.max_x <= a.min_x) {
		return false;
	}
	if (a.max_y <= b.min_y || b.max_y <= a.min_y) {
		return false;
	}
	return true;
}

/// ¿`inner` cabe entero dentro de `outer`? (bordes exclusivos).
[[nodiscard]] constexpr bool aabb_contains(const Aabb& outer, const Aabb& inner) noexcept {
	return inner.min_x >= outer.min_x && inner.min_y >= outer.min_y &&
	       inner.max_x <= outer.max_x && inner.max_y <= outer.max_y;
}

[[nodiscard]] constexpr bool point_in_aabb(const Aabb& box, Point2s p) noexcept {
	return p.x >= box.min_x && p.x < box.max_x && p.y >= box.min_y && p.y < box.max_y;
}

/// Signo de la orientación de `(a-o)×(b-o)`: `>0` a la izquierda, `<0` a la derecha,
/// `0` colineal. Productos con `muls.w`.
[[nodiscard]] constexpr s32 orient(Point2s o, Point2s a, Point2s b) noexcept {
	const s16 ax = static_cast<s16>(a.x - o.x);
	const s16 ay = static_cast<s16>(a.y - o.y);
	const s16 bx = static_cast<s16>(b.x - o.x);
	const s16 by = static_cast<s16>(b.y - o.y);
	return eng::math::mul_wide(ax, by) - eng::math::mul_wide(ay, bx);
}

/// ¿`p` está en el segmento `[a,b]` (asumiendo colinealidad)?
[[nodiscard]] constexpr bool on_segment(Point2s a, Point2s b, Point2s p) noexcept {
	const bool in_x = p.x >= (a.x < b.x ? a.x : b.x) && p.x <= (a.x < b.x ? b.x : a.x);
	const bool in_y = p.y >= (a.y < b.y ? a.y : b.y) && p.y <= (a.y < b.y ? b.y : a.y);
	return in_x && in_y;
}

/// ¿Se cruzan los segmentos `[a,b]` y `[c,d]`? (incluye colinealidad y contacto).
[[nodiscard]] constexpr bool segments_intersect(Point2s a, Point2s b, Point2s c, Point2s d) noexcept {
	const s32 d1 = orient(c, d, a);
	const s32 d2 = orient(c, d, b);
	const s32 d3 = orient(a, b, c);
	const s32 d4 = orient(a, b, d);

	if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
	    ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
		return true;
	}
	if (d1 == 0 && on_segment(c, d, a)) return true;
	if (d2 == 0 && on_segment(c, d, b)) return true;
	if (d3 == 0 && on_segment(a, b, c)) return true;
	if (d4 == 0 && on_segment(a, b, d)) return true;
	return false;
}

/// ¿`p` dentro del triángulo `abc`? (incluye bordes).
[[nodiscard]] constexpr bool point_in_triangle(Point2s p, Point2s a, Point2s b,
					       Point2s c) noexcept {
	const s32 d1 = orient(a, b, p);
	const s32 d2 = orient(b, c, p);
	const s32 d3 = orient(c, a, p);
	const bool has_neg = d1 < 0 || d2 < 0 || d3 < 0;
	const bool has_pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !(has_neg && has_pos);
}

/// ¿`p` dentro del polígono **convexo** `poly`? (incluye bordes). Todos los productos
/// de `orient` deben tener el mismo signo. `poly` debe ser convexo y en orden.
[[nodiscard]] constexpr bool point_in_convex(Point2s p, Span<const Point2s> poly) noexcept {
	if (poly.size() < 3u) {
		return false;
	}
	bool pos = false;
	bool neg = false;
	for (usize i = 0u; i < poly.size(); ++i) {
		const s32 c = orient(poly[i], poly[(i + 1u) % poly.size()], p);
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

namespace detail {

/// Intervalo `[mn,mx]` de la proyección de `poly` sobre el eje `(nx, ny)` (sin
/// normalizar). Productos 16×16 con `muls.w`.
constexpr void project_poly(Span<const Point2s> poly, s16 nx, s16 ny, s32& mn,
			    s32& mx) noexcept {
	const Point2s p0 = poly[0];
	mn = mx = static_cast<s32>(eng::math::mul_wide(nx, p0.x) + eng::math::mul_wide(ny, p0.y));
	for (usize i = 1u; i < poly.size(); ++i) {
		const s32 d =
			static_cast<s32>(eng::math::mul_wide(nx, poly[i].x) + eng::math::mul_wide(ny, poly[i].y));
		if (d < mn) {
			mn = d;
		} else if (d > mx) {
			mx = d;
		}
	}
}

/// ¿Los intervalos `[amn,amx]` y `[bmn,bmx]` se separan? (tocar por el borde cuenta como
/// separado, igual que `aabb_overlap`).
constexpr bool separated(const s32 amn, const s32 amx, const s32 bmn,
			 const s32 bmx) noexcept {
	return amx <= bmn || bmx <= amn;
}

} // namespace detail

/// ¿Se solapan dos polígonos **convexos**? **Teorema de los ejes separadores (SAT)**: si
/// algún eje de arista deja las proyecciones separadas, no se tocan. Los ejes van sin
/// normalizar (solo importa el signo), así que no hay `sqrt` ni división. Coordenadas en
/// `s16` y diferencias que quepan en `s16` (≈±16000), como el resto del archivo.
[[nodiscard]] constexpr bool convex_overlap(Span<const Point2s> a,
					    Span<const Point2s> b) noexcept {
	if (a.size() < 3u || b.size() < 3u) {
		return false;
	}
	// Ejes de las aristas de `a`.
	for (usize i = 0u; i < a.size(); ++i) {
		const Point2s p = a[i];
		const Point2s q = a[(i + 1u) % a.size()];
		const s16 nx = static_cast<s16>(q.y - p.y);
		const s16 ny = static_cast<s16>(-(q.x - p.x));
		s32 amn = 0;
		s32 amx = 0;
		s32 bmn = 0;
		s32 bmx = 0;
		detail::project_poly(a, nx, ny, amn, amx);
		detail::project_poly(b, nx, ny, bmn, bmx);
		if (detail::separated(amn, amx, bmn, bmx)) {
			return false;
		}
	}
	// Ejes de las aristas de `b`.
	for (usize i = 0u; i < b.size(); ++i) {
		const Point2s p = b[i];
		const Point2s q = b[(i + 1u) % b.size()];
		const s16 nx = static_cast<s16>(q.y - p.y);
		const s16 ny = static_cast<s16>(-(q.x - p.x));
		s32 amn = 0;
		s32 amx = 0;
		s32 bmn = 0;
		s32 bmx = 0;
		detail::project_poly(a, nx, ny, amn, amx);
		detail::project_poly(b, nx, ny, bmn, bmx);
		if (detail::separated(amn, amx, bmn, bmx)) {
			return false;
		}
	}
	return true;
}

/// ¿Se solapan dos círculos? Compara distancias al cuadrado (`muls.w`), sin `sqrt`.
[[nodiscard]] constexpr bool circle_overlap(Point2s c0, s16 r0, Point2s c1, s16 r1) noexcept {
	const s16 dx = static_cast<s16>(c1.x - c0.x);
	const s16 dy = static_cast<s16>(c1.y - c0.y);
	const s32 dist_sq = eng::math::mul_wide(dx, dx) + eng::math::mul_wide(dy, dy);
	const s16 rsum = static_cast<s16>(r0 + r1);
	return dist_sq <= eng::math::mul_wide(rsum, rsum);
}

} // namespace eng::util
