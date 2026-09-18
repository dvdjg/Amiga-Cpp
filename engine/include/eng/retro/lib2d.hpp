#pragma once

/// \file lib2d.hpp
/// **lib2d retro**: geometría 2D en 4.12 sobre los tipos GENÉRICOS del núcleo.
///
/// `Vec2`/`Mat2x2`/`Rect` son alias de `eng::math::Vec<2,q0>` /
/// `Affine<2,q12,q0>` / `Rect<q0>`: los mismos datos que los `Point2D` /
/// `Matrix2D` / `Box2D` del original (LONGITUD en píxeles + parte lineal 4.12),
/// pero tipados y reutilizando el álgebra genérica (`transform`, `rescale`).
///
/// El recorte (Liang-Barsky y Sutherland-Hodgman) opera en píxeles enteros; los
/// parámetros `t` de interpolación son 8.8 (`fix88`), como el original.

#include <eng/core/linalg.hpp>
#include <eng/core/types.hpp>
#include <eng/core/word.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::retro {

/// Punto/vector 2D en píxeles (LONGITUD).
using Vec2 = eng::math::Vec<2, q0>;
/// Transformación 2D: parte lineal 4.12 (RATIO) + traslación en píxeles (LONGITUD).
using Mat2x2 = eng::math::Affine<2, q12, q0>;
/// Ventana de recorte en píxeles.
using Rect = eng::math::Rect<q0>;

/// Construye un `Vec2` desde LONGITUD tipada (`q0`).
constexpr Vec2 v2(q0 x, q0 y) { return Vec2 {{x, y}}; }
/// Construye un `Vec2` desde píxeles crudos (azúcar para literales).
constexpr Vec2 v2(s16 x, s16 y) { return Vec2 {{q0 {x}, q0 {y}}}; }
/// Construye un `Rect` desde LONGITUD tipada (`q0`).
constexpr Rect rect(q0 x0, q0 y0, q0 x1, q0 y1) { return Rect {x0, y0, x1, y1}; }
/// Construye un `Rect` desde píxeles crudos (azúcar para literales).
constexpr Rect rect(s16 x0, s16 y0, s16 x1, s16 y1) {
	return Rect {q0 {x0}, q0 {y0}, q0 {x1}, q0 {y1}};
}

/// Suma una traslación (`q0`, píxeles) a la matriz; no toca la parte lineal.
constexpr void translate(Mat2x2& m, q0 dx, q0 dy) {
	m.t.x() = q0 {static_cast<s16>(m.t.x().v + dx.v)};
	m.t.y() = q0 {static_cast<s16>(m.t.y().v + dy.v)};
}

/// Escala la parte lineal (factores RATIO en 4.12, el mismo escalar que la matriz).
constexpr void scale(Mat2x2& m, q12 sx, q12 sy) {
	for (int i = 0; i < 2; ++i) {
		m.m.m[i][0] = eng::math::mul_norm(m.m.m[i][0], sx);
		m.m.m[i][1] = eng::math::mul_norm(m.m.m[i][1], sy);
	}
}

// ============================================================================
//  Recorte 2D (píxeles enteros)
// ============================================================================

// Lados de la ventana de recorte (bits, igual que `PF_LEFT..PF_BOTTOM`).
constexpr u8 PF_LEFT = 1u;
constexpr u8 PF_RIGHT = 2u;
constexpr u8 PF_TOP = 4u;
constexpr u8 PF_BOTTOM = 8u;

/// Marca cada punto según en qué lado de `win` cae (bits `PF_*`, como
/// `PointsInsideBox`). Devuelve la máscara combinada.
constexpr u8 point_flags(const Vec2& p, const Rect& win) {
	const s16 px = p.x().v;
	const s16 py = p.y().v;
	u8 f = 0;
	if (px < win.minX.v) f |= PF_LEFT;
	else if (px >= win.maxX.v) f |= PF_RIGHT;
	if (py < win.minY.v) f |= PF_TOP;
	else if (py >= win.maxY.v) f |= PF_BOTTOM;
	return f;
}

/// Recorta el segmento `a`–`b` contra `win` (Liang-Barsky, como `ClipLine2D`).
/// Actualiza `a`/`b` si hace falta; `t` en 8.8 (0..`kOne88` = 0.0..1.0).
inline bool clip_line(const Rect& win, Vec2& a, Vec2& b) {
	s16 t0 = 0;
	s16 t1 = static_cast<s16>(kOne88);
	const s16 ax = a.x().v, ay = a.y().v;
	const s16 bx = b.x().v, by = b.y().v;
	const s16 xd = static_cast<s16>(bx - ax);
	const s16 yd = static_cast<s16>(by - ay);

	struct Edge {
		s16 p, q1, q2;
	};
	const Edge edge[2] = {
		{static_cast<s16>(-xd), static_cast<s16>(ax - win.minX.v),
		 static_cast<s16>(win.maxX.v - ax)},
		{static_cast<s16>(-yd), static_cast<s16>(ay - win.minY.v),
		 static_cast<s16>(win.maxY.v - ay)},
	};

	for (u32 i = 0; i < 2u; ++i) {
		const s16 p = edge[i].p;
		if (p == 0) {
			continue;
		}
		if (p < 0) {
			s16 r = eng::math::div_wide(static_cast<s32>(edge[i].q1) << kShift88, p);
			if (r > t1) return false;
			if (r > t0) t0 = r;
			r = eng::math::div_wide(static_cast<s32>(edge[i].q2) << kShift88,
					     static_cast<s16>(-p));
			if (r < t0) return false;
			if (r < t1) t1 = r;
		} else {
			s16 r = eng::math::div_wide(static_cast<s32>(edge[i].q1) << kShift88, p);
			if (r < t0) return false;
			if (r < t1) t1 = r;
			r = eng::math::div_wide(static_cast<s32>(edge[i].q2) << kShift88,
					     static_cast<s16>(-p));
			if (r > t1) return false;
			if (r > t0) t0 = r;
		}
	}

	if (t0 > 0) {
		a.x().v = static_cast<s16>(ax + ((eng::math::mul_wide(t0, xd) + kHalf88) >> kShift88));
		a.y().v = static_cast<s16>(ay + ((eng::math::mul_wide(t0, yd) + kHalf88) >> kShift88));
	}
	if (t1 < kOne88) {
		const s16 t1r = static_cast<s16>(kOne88 - t1);
		b.x().v = static_cast<s16>(bx - ((eng::math::mul_wide(t1r, xd) + kHalf88) >> kShift88));
		b.y().v = static_cast<s16>(by - ((eng::math::mul_wide(t1r, yd) + kHalf88) >> kShift88));
	}
	return true;
}

/// ¿Está `p` dentro del semi-plano `plane` respecto a `win`? (`CheckInside`).
constexpr bool clip_inside(const Vec2& p, const Rect& win, u16 plane) {
	if (plane & PF_LEFT) return p.x().v >= win.minX.v;
	if (plane & PF_RIGHT) return p.x().v < win.maxX.v;
	if (plane & PF_TOP) return p.y().v >= win.minY.v;
	if (plane & PF_BOTTOM) return p.y().v < win.maxY.v;
	return false;
}

/// Intersección de la arista `s`–`e` con el semi-plano `plane` (`ClipEdge`).
inline void clip_edge(const Rect& win, Vec2& o, const Vec2& s, const Vec2& e, u16 plane) {
	const s16 sx = s.x().v, sy = s.y().v;
	const s16 ex = e.x().v, ey = e.y().v;
	const s16 dx = static_cast<s16>(sx - ex);
	const s16 dy = static_cast<s16>(sy - ey);
	if (plane & PF_LEFT) {
		const s16 n = static_cast<s16>(win.minX.v - ex);
		o.x().v = win.minX.v;
		o.y().v = static_cast<s16>(ey + eng::math::div_wide(eng::math::mul_wide(dy, n), dx));
	} else if (plane & PF_RIGHT) {
		const s16 n = static_cast<s16>(win.maxX.v - ex);
		o.x().v = win.maxX.v;
		o.y().v = static_cast<s16>(ey + eng::math::div_wide(eng::math::mul_wide(dy, n), dx));
	} else if (plane & PF_TOP) {
		const s16 n = static_cast<s16>(win.minY.v - ey);
		o.x().v = static_cast<s16>(ex + eng::math::div_wide(eng::math::mul_wide(dx, n), dy));
		o.y().v = win.minY.v;
	} else if (plane & PF_BOTTOM) {
		const s16 n = static_cast<s16>(win.maxY.v - ey);
		o.x().v = static_cast<s16>(ex + eng::math::div_wide(eng::math::mul_wide(dx, n), dy));
		o.y().v = win.maxY.v;
	}
}

/// Una pasada de Sutherland-Hodgman (recorta contra un solo semi-plano).
/// `src`/`dst` deben tener capacidad >= n+1. Devuelve los vértices escritos en `dst`.
inline u32 clip_polygon_pass(const Rect& win, const Vec2* src, Vec2* dst, u32 n, u16 plane) {
	if (n == 0u) return 0u;
	const Vec2* s = src;
	const Vec2* e = src + 1;
	bool s_in = clip_inside(*s, win, plane);
	bool need_close = true;
	u32 m = 0;
	if (s_in) {
		need_close = false;
		dst[m++] = *s;
	}
	while (--n) {
		const bool e_in = clip_inside(*e, win, plane);
		if (s_in && e_in) {
			dst[m++] = *e;
		} else if (s_in && !e_in) {
			clip_edge(win, dst[m++], *s, *e, plane);
		} else if (!s_in && e_in) {
			clip_edge(win, dst[m++], *e, *s, plane);
			dst[m++] = *e;
		}
		s_in = e_in;
		++s;
		++e;
	}
	if (need_close) {
		dst[m++] = dst[0];
	}
	return m;
}

/// Recorta el polígono `in` (n vértices) contra `win` con las aristas `clip_flags`
/// (en orden LEFT, TOP, RIGHT, BOTTOM, como `ClipPolygon2D`). `tmp` es un buffer de
/// trabajo; el resultado queda en `in` y se devuelve su número de vértices.
inline u32 clip_polygon(const Rect& win, Vec2* in, Vec2* tmp, u32 n, u8 clip_flags) {
	Vec2* src = in;
	Vec2* dst = tmp;
	auto pass = [&](u16 plane) {
		n = clip_polygon_pass(win, src, dst, n, plane);
		Vec2* t = src;
		src = dst;
		dst = t;
	};
	if (clip_flags & PF_LEFT) pass(PF_LEFT);
	if (clip_flags & PF_TOP) pass(PF_TOP);
	if (clip_flags & PF_RIGHT) pass(PF_RIGHT);
	if (clip_flags & PF_BOTTOM) pass(PF_BOTTOM);
	if (src != in) {
		for (u32 i = 0; i < n; ++i) in[i] = src[i];
	}
	return n;
}

} // namespace eng::retro
