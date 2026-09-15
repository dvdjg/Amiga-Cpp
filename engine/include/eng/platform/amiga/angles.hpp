#pragma once

/// \file angles.hpp
/// **Ángulos 4.12** (tabla de seno exacta de 4096 pasos) para gráficos retro. Es una
/// especialización de plataforma: un PC moderno usaría `float` y no necesita esta tabla,
/// ni la convención de que un ángulo sea un índice 0..4095.
///
/// Es la tabla **exacta del original** (`kSinTableQ12`, `eng/core/sinetable.hpp`): los efectos
/// portados 1:1 la necesitan. El álgebra de rotaciones
/// (que sí es genérica) está en `eng/core/linalg.hpp`; aquí sólo vive el formato.

#include <eng/core/math2d.hpp>
#include <eng/core/types.hpp>
#include <eng/core/sinetable.hpp>

namespace eng::math2d {

/// π/2 como índice de ángulo (4096 pasos por vuelta).
constexpr u16 kHalfPi = 1024;
/// Pasos por vuelta de la tabla de seno.
constexpr u32 kAngleSteps = 4096;

/// Tabla de seno 4.12 materializada.
struct SinTableQ12 {
	s16 v[kAngleSteps] {};
	constexpr SinTableQ12() {
		for (u32 i = 0; i < kAngleSteps; ++i) {
			v[i] = static_cast<s16>(eng::kSinTab[i]);
		}
	}
};

inline constexpr SinTableQ12 kSinQ12 {};

/// Seno de un ángulo `a` (0..4095 = 0..2π) en 4.12.
constexpr fix sin_q12(u16 a) { return kSinQ12.v[a & (kAngleSteps - 1u)]; }
/// Coseno de un ángulo `a` en 4.12.
constexpr fix cos_q12(u16 a) { return kSinQ12.v[(a + kHalfPi) & (kAngleSteps - 1u)]; }

/// Rota la parte lineal de una `Mat2x2` por el ángulo `a` (igual que `Rotate2D`).
inline void rotate(Mat2x2& m, u16 a) {
	const fix s = sin_q12(a);
	const fix c = cos_q12(a);
	const fix m00 = m.m00, m01 = m.m01, m10 = m.m10, m11 = m.m11;
	m.m00 = normfx(static_cast<s32>(m00) * c - static_cast<s32>(m01) * s);
	m.m01 = normfx(static_cast<s32>(m00) * s + static_cast<s32>(m01) * c);
	m.m10 = normfx(static_cast<s32>(m10) * c - static_cast<s32>(m11) * s);
	m.m11 = normfx(static_cast<s32>(m10) * s + static_cast<s32>(m11) * c);
}

} // namespace eng::math2d
