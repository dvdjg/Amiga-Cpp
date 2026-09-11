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

} // namespace eng::math2d
