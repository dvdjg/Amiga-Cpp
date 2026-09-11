#pragma once

/// \file math2d.hpp
/// Matemática 2D en **fixed-point 4.12** (port de `lib2d` de demoscene-repo-orig).
///
/// Porta la parte pura de `lib2d`: matrices 2×2 + traslación (`Matrix2D`),
/// transformación de puntos y la tabla de seno de 4096 pasos. Es API pura (sin
/// hardware), por lo que vive en `eng::core` y se valida con test host.
///
/// Formato numérico (idéntico al origen):
/// - Los valores son `s16` en 4.12: `1.0 == 4096`. La parte entera son 4 bits con
///   signo y la fracción 12 bits.
/// - El producto de dos 4.12 es 8.24; `normfx(a) = a >> 12` lo normaliza a 4.12
///   (shift aritmético, como el `lsll #4/swap` del 68000 sobre el short).
/// - Los ángulos son índices de 0..4095 para 0..2π (`SIN(a & 4095)`).
///
/// El `<eng/core/sinetable.hpp>` del engine ya genera con `SineTable` (constexpr,
/// sin libm en runtime) los valores; aquí se materializa la tabla 4.12 de 4096
/// entradas reutilizando `SineTable<4096, 4096>::sample`.

#include <eng/core/sinetable.hpp>
#include <eng/core/types.hpp>

namespace eng::math2d {

/// Valor fixed-point 4.12.
using fix = s16;

/// 1.0 en 4.12.
constexpr fix kOne = 4096;
/// π/2 como índice de ángulo (4096 pasos por vuelta).
constexpr u16 kHalfPi = 1024;
/// Pasos por vuelta de la tabla de seno.
constexpr u32 kAngleSteps = 4096;

/// Entero a 4.12.
constexpr fix fx12i(int i) { return static_cast<fix>(i * 4096); }

/// Normaliza un producto 8.24 a 4.12 (shift aritmético a la derecha de 12).
/// Equivale al `normfx` del origen (`lsll #4; swap`).
constexpr fix normfx(s32 a) { return static_cast<fix>(a >> 12); }

/// Punto/vector 2D (mismos campos que `Point2D` del origen).
struct Vec2 {
	s16 x = 0;
	s16 y = 0;
};

/// Rectángulo (mismos campos que `Box2D`).
struct Rect {
	s16 minX = 0;
	s16 minY = 0;
	s16 maxX = 0;
	s16 maxY = 0;
};

/// Matriz 2×2 + traslación, en 4.12 (mismos campos que `Matrix2D`).
struct Mat2x2 {
	fix m00 = kOne;
	fix m01 = 0;
	fix x = 0;
	fix m10 = 0;
	fix m11 = kOne;
	fix y = 0;
};

/// Tabla de seno 4.12 (4096 pasos = 2π), generada en compile-time.
struct SinTableQ12 {
	s16 v[kAngleSteps] {};
	constexpr SinTableQ12() {
		for (u32 i = 0; i < kAngleSteps; ++i) {
			v[i] = static_cast<s16>(SineTable<4096, kAngleSteps>::sample(i));
		}
	}
};

inline constexpr SinTableQ12 kSinQ12 {};

/// Seno de un ángulo `a` (0..4095 = 0..2π) en 4.12.
constexpr fix sin_q12(u16 a) { return kSinQ12.v[a & (kAngleSteps - 1u)]; }
/// Coseno de un ángulo `a` en 4.12.
constexpr fix cos_q12(u16 a) { return kSinQ12.v[(a + kHalfPi) & (kAngleSteps - 1u)]; }

/// Deja `m` como la identidad.
constexpr void load_identity(Mat2x2& m) {
	m = Mat2x2 {};
}

/// Suma una traslación (no toca la parte lineal).
constexpr void translate(Mat2x2& m, s16 x, s16 y) {
	m.x = static_cast<fix>(m.x + x);
	m.y = static_cast<fix>(m.y + y);
}

/// Escala la parte lineal (factores en 4.12).
constexpr void scale(Mat2x2& m, fix sx, fix sy) {
	m.m00 = normfx(static_cast<s32>(m.m00) * sx);
	m.m01 = normfx(static_cast<s32>(m.m01) * sy);
	m.m10 = normfx(static_cast<s32>(m.m10) * sx);
	m.m11 = normfx(static_cast<s32>(m.m11) * sy);
}

/// Rota la parte lineal por el ángulo `a` (0..4095 = 0..2π). Igual que `Rotate2D`.
constexpr void rotate(Mat2x2& m, u16 a) {
	const fix s = sin_q12(a);
	const fix c = cos_q12(a);
	const fix m00 = m.m00, m01 = m.m01, m10 = m.m10, m11 = m.m11;
	m.m00 = normfx(static_cast<s32>(m00) * c - static_cast<s32>(m01) * s);
	m.m01 = normfx(static_cast<s32>(m00) * s + static_cast<s32>(m01) * c);
	m.m10 = normfx(static_cast<s32>(m10) * c - static_cast<s32>(m11) * s);
	m.m11 = normfx(static_cast<s32>(m10) * s + static_cast<s32>(m11) * c);
}

/// Aplica `m` a `n` puntos (igual que `Transform2D`): `out = M·in + traslación`.
inline void transform(const Mat2x2& m, Vec2* out, const Vec2* in, u32 n) {
	for (u32 i = 0; i < n; ++i) {
		const s16 x = in[i].x;
		const s16 y = in[i].y;
		out[i].x = static_cast<s16>(normfx(static_cast<s32>(m.m00) * x + static_cast<s32>(m.m01) * y) + m.x);
		out[i].y = static_cast<s16>(normfx(static_cast<s32>(m.m10) * x + static_cast<s32>(m.m11) * y) + m.y);
	}
}

/// Marca cada punto según en qué lado de `win` cae (bits `PF_*`, como
/// `PointsInsideBox`). Devuelve la máscara combinada de todos los puntos.
constexpr u8 point_flags(const Vec2& p, const Rect& win) {
	u8 f = 0;
	if (p.x < win.minX) f |= 1u;        // LEFT
	else if (p.x >= win.maxX) f |= 2u;  // RIGHT
	if (p.y < win.minY) f |= 4u;        // TOP
	else if (p.y >= win.maxY) f |= 8u;  // BOTTOM
	return f;
}

// Lados de la ventana de recorte (bits, igual que `PF_LEFT..PF_BOTTOM`).
constexpr u8 PF_LEFT = 1u;
constexpr u8 PF_RIGHT = 2u;
constexpr u8 PF_TOP = 4u;
constexpr u8 PF_BOTTOM = 8u;

/// División entera con cociente a 16 bits (equivale al `div16` del origen, `divs.w`).
/// Requiere `b != 0` y cociente representable en `s16`.
inline s16 div16(s32 a, s16 b) { return static_cast<s16>(a / b); }

/// Recorta el segmento `a`–`b` contra `win` (algoritmo de Liang-Barsky, igual que
/// `ClipLine2D`). Actualiza `a`/`b` si hace falta y devuelve `true` si queda parte
/// visible. Los `t` están en 8.8 (0..256).
inline bool clip_line(const Rect& win, Vec2& a, Vec2& b) {
	constexpr s32 kBits = 8;
	constexpr s32 kOne = 1 << kBits;
	constexpr s32 kHalf = 1 << (kBits - 1);

	s16 t0 = 0;
	s16 t1 = static_cast<s16>(kOne);
	const s16 xd = static_cast<s16>(b.x - a.x);
	const s16 yd = static_cast<s16>(b.y - a.y);

	struct Edge { s16 p, q1, q2; };
	const Edge edge[2] = {
		{ static_cast<s16>(-xd), static_cast<s16>(a.x - win.minX), static_cast<s16>(win.maxX - a.x) },
		{ static_cast<s16>(-yd), static_cast<s16>(a.y - win.minY), static_cast<s16>(win.maxY - a.y) },
	};

	for (u32 i = 0; i < 2u; ++i) {
		const s16 p = edge[i].p;
		if (p == 0) {
			continue;
		}
		if (p < 0) {
			s16 r = div16(static_cast<s32>(edge[i].q1) << kBits, p);
			if (r > t1) return false;
			if (r > t0) t0 = r;
			r = div16(static_cast<s32>(edge[i].q2) << kBits, static_cast<s16>(-p));
			if (r < t0) return false;
			if (r < t1) t1 = r;
		} else {
			s16 r = div16(static_cast<s32>(edge[i].q1) << kBits, p);
			if (r < t0) return false;
			if (r < t1) t1 = r;
			r = div16(static_cast<s32>(edge[i].q2) << kBits, static_cast<s16>(-p));
			if (r > t1) return false;
			if (r > t0) t0 = r;
		}
	}

	if (t0 > 0) {
		a.x = static_cast<s16>(a.x + ((static_cast<s32>(t0) * xd + kHalf) >> kBits));
		a.y = static_cast<s16>(a.y + ((static_cast<s32>(t0) * yd + kHalf) >> kBits));
	}
	if (t1 < kOne) {
		const s16 t1r = static_cast<s16>(kOne - t1);
		b.x = static_cast<s16>(b.x - ((static_cast<s32>(t1r) * xd + kHalf) >> kBits));
		b.y = static_cast<s16>(b.y - ((static_cast<s32>(t1r) * yd + kHalf) >> kBits));
	}
	return true;
}

/// ¿Está `p` dentro del semi-plano `plane` respecto a `win`? (`CheckInside`).
constexpr bool clip_inside(const Vec2& p, const Rect& win, u16 plane) {
	if (plane & PF_LEFT) return p.x >= win.minX;
	if (plane & PF_RIGHT) return p.x < win.maxX;
	if (plane & PF_TOP) return p.y >= win.minY;
	if (plane & PF_BOTTOM) return p.y < win.maxY;
	return false;
}

/// Intersección de la arista `s`–`e` con el semi-plano `plane` (`ClipEdge`).
inline void clip_edge(const Rect& win, Vec2& o, const Vec2& s, const Vec2& e, u16 plane) {
	const s16 dx = static_cast<s16>(s.x - e.x);
	const s16 dy = static_cast<s16>(s.y - e.y);
	if (plane & PF_LEFT) {
		const s16 n = static_cast<s16>(win.minX - e.x);
		o.x = win.minX;
		o.y = static_cast<s16>(e.y + div16(static_cast<s32>(dy) * n, dx));
	} else if (plane & PF_RIGHT) {
		const s16 n = static_cast<s16>(win.maxX - e.x);
		o.x = win.maxX;
		o.y = static_cast<s16>(e.y + div16(static_cast<s32>(dy) * n, dx));
	} else if (plane & PF_TOP) {
		const s16 n = static_cast<s16>(win.minY - e.y);
		o.x = static_cast<s16>(e.x + div16(static_cast<s32>(dx) * n, dy));
		o.y = win.minY;
	} else if (plane & PF_BOTTOM) {
		const s16 n = static_cast<s16>(win.maxY - e.y);
		o.x = static_cast<s16>(e.x + div16(static_cast<s32>(dx) * n, dy));
		o.y = win.maxY;
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
/// trabajo; `in` y `tmp` deben tener capacidad >= n+1. El resultado queda en `in`
/// y se devuelve su número de vértices.
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

} // namespace eng::math2d
