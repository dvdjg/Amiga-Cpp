#pragma once

/// \file angles.hpp
/// **Ángulos 4.12** (tabla de seno exacta de 4096 pasos) y la rotación 2D que la usa.
/// Es vocabulario retro: un PC moderno usaría `float` y no necesitaría ni la tabla ni la
/// convención de que un ángulo sea un índice 0..4095.
///
/// La tabla (`kSinTab`) es la 4.12 exacta del original (`eng/retro/sintab.hpp`); el álgebra
/// de rotaciones (genérica) está en `eng/core/linalg.hpp`. Aquí sólo vive el formato.

#include <eng/core/types.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/retro/lib2d.hpp>
#include <eng/retro/sintab.hpp>

namespace eng::retro {

/// π/2 como índice de ángulo (4096 pasos por vuelta).
constexpr u16 kHalfPi = 1024;
/// Pasos por vuelta de la tabla de seno.
constexpr u32 kAngleSteps = 4096;

/// Seno de un ángulo `a` (0..4095 = 0..2π) en 4.12.
constexpr fix sin_q12(u16 a) { return kSinTab[a & (kAngleSteps - 1u)]; }
/// Coseno de un ángulo `a` en 4.12.
constexpr fix cos_q12(u16 a) { return kSinTab[(a + kHalfPi) & (kAngleSteps - 1u)]; }

/// Rota la parte lineal de una `Mat2x2` por el ángulo `a` (igual que `Rotate2D`).
///
/// Nota: se mantiene el par `sin_q12`/`cos_q12` en vez de un `sincos` combinado porque
/// a `-O2` gcc **reutiliza el índice** de la tabla entre ambos: medido con una sonda
/// (out/tmp/sincos-probe.cpp), el par separado son 30 instr y un `sincos` explícito 36.
inline void rotate(Mat2x2& m, u16 a) {
	const fix s = sin_q12(a);
	const fix c = cos_q12(a);
	const s16 m00 = m.m.m[0][0].v, m01 = m.m.m[0][1].v;
	const s16 m10 = m.m.m[1][0].v, m11 = m.m.m[1][1].v;
	m.m.m[0][0] = q12 {normfx(static_cast<s32>(m00) * c - static_cast<s32>(m01) * s)};
	m.m.m[0][1] = q12 {normfx(static_cast<s32>(m00) * s + static_cast<s32>(m01) * c)};
	m.m.m[1][0] = q12 {normfx(static_cast<s32>(m10) * c - static_cast<s32>(m11) * s)};
	m.m.m[1][1] = q12 {normfx(static_cast<s32>(m10) * s + static_cast<s32>(m11) * c)};
}

} // namespace eng::retro
