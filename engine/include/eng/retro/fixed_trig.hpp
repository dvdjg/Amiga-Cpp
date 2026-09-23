#pragma once

/// \file fixed_trig.hpp
/// **Vocabulario trigonométrico retro** (Q 4.12): la tabla de seno exacta del original
/// (`kSinTab`, 4096 pasos) y sus dos vistas.
///
/// ## Dos vistas de la misma tabla
///
/// - **Genérica** (`scalar_sin`/`scalar_cos`/`scalar_sincos<Fixed<s16,12,P>>`): el ángulo
///   va en **radianes** (igual que en `float`/`double` y que en `fixed_math.hpp`). Es la
///   que usan los algoritmos genéricos (p. ej. `math3d::load_rotate`), de modo que un mismo
///   algoritmo se instancia con `float` o con `q12` sin cambios.
/// - **Ángulo en vueltas** (`Turns` + `sin`/`cos`): la convención del original, con el
///   índice `0..4095` (vueltas) **en el tipo**, no en el nombre de la función. Es la que
///   usan los generadores de tablas que necesitan la posición exacta (`plasma`, tests de
///   la tabla). `angle_to_radians` convierte vueltas/índice a radianes para las APIs
///   genéricas.
///
/// Sólo esta cabecera (retro) nombra `Fixed<s16,12>`; el núcleo no conoce el formato Q.

#include <eng/core/math/fixed.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/types/types.hpp>
#include <eng/retro/fixed_q.hpp>
#include <eng/retro/sintab.hpp>

namespace eng::retro {

/// π/2 como índice de ángulo (4096 pasos por vuelta).
inline constexpr u16 kHalfPi = 1024;
/// Pasos por vuelta de la tabla de seno.
inline constexpr u32 kAngleSteps = 4096;

/// Ángulo medido en **vueltas** sobre `q12`: es `Angle<q12, angle::turns>` (la unidad va
/// en el tipo, así que `sin`/`cos` son genéricas y no hay `cos_q12`).
using Turns = eng::math::Angle<q12, eng::math::angle::turns>;

/// Construye un ángulo en vueltas desde el índice entero `0..4095` del original.
constexpr Turns turns(u16 a) { return Turns {q12 {static_cast<s16>(a)}}; }

/// Reexporta `sin`/`cos` genéricos para que `eng::retro::sin(angle)` funcione.
using eng::math::cos;
using eng::math::sin;

/// Ángulo en **radianes** sobre `q12`: es `Angle<q12, angle::radians>`.
using Radians = eng::math::Angle<q12, eng::math::angle::radians>;

/// Construye un ángulo en radianes desde un `q12` (el escalar ya en radianes).
constexpr Radians radians(q12 a) { return Radians {a}; }

/// Convierte un **índice** de ángulo (0..4095 = 0..2π) a un ángulo en radianes. Redondeo
/// al más cercano; el viaje índice→radianes→índice es exacto (se comprueba en HOST-177).
constexpr Radians angle_to_radians(u32 steps) {
	static_assert(sizeof(q12) == 2, "angle_to_radians: q12 es de 16 bits");
	return Radians {q12 {static_cast<s16>(
		(static_cast<s32>(steps & (kAngleSteps - 1u)) * 411774 + 32768) >> 16)}};
}

} // namespace eng::retro

namespace eng::math {

/// `sin`/`cos` de un ángulo en vueltas sobre `q12`: tabla 4.12 **exacta** del original
/// (el índice es el crudo del 4.12, así que el viaje es exacto).
template <typename P>
struct angle_sin<Fixed<s16, 12, P>, angle::turns> {
	static constexpr Fixed<s16, 12, P> op(Angle<Fixed<s16, 12, P>, angle::turns> a) {
		return Fixed<s16, 12, P> {
			eng::retro::kSinQ12[static_cast<u16>(a.value.v) & (eng::retro::kAngleSteps - 1u)]};
	}
};
template <typename P>
struct angle_cos<Fixed<s16, 12, P>, angle::turns> {
	static constexpr Fixed<s16, 12, P> op(Angle<Fixed<s16, 12, P>, angle::turns> a) {
		return Fixed<s16, 12, P> {eng::retro::kSinQ12[(static_cast<u16>(a.value.v) +
								eng::retro::kHalfPi) &
							       (eng::retro::kAngleSteps - 1u)]};
	}
};

/// `sincos` de un ángulo en vueltas sobre `q12`: un índice, dos lecturas de tabla.
template <typename P>
struct angle_sincos<Fixed<s16, 12, P>, angle::turns> {
	static constexpr void op(Angle<Fixed<s16, 12, P>, angle::turns> a,
				 Fixed<s16, 12, P>& out_sin, Fixed<s16, 12, P>& out_cos) {
		const s32 idx = static_cast<u16>(a.value.v);
		out_sin = Fixed<s16, 12, P> {eng::retro::kSinQ12[idx & (eng::retro::kAngleSteps - 1u)]};
		out_cos = Fixed<s16, 12, P> {
			eng::retro::kSinQ12[(idx + eng::retro::kHalfPi) & (eng::retro::kAngleSteps - 1u)]};
	}
};

namespace detail {

/// `1/(2π)` en 16.16 (redondeado): `indice = (raw · kInv2PiFx + 0.5) >> 16`.
inline constexpr s32 kInv2PiFx = 10430;
/// 0.5 en 16.16 (redondeo al más cercano en el paso a índice).
inline constexpr s32 kFxHalf = 32768;
/// Máscara de la tabla (4096 pasos).
inline constexpr s32 kRetroSinMask = 4095;
/// Un cuarto de vuelta (desfase del coseno).
inline constexpr s32 kRetroSinQuarter = 1024;

/// Índice de `kSinTab` para un ángulo en radianes 4.12 (redondeo al más cercano).
[[nodiscard]] constexpr s32 retro_sin_index(s32 raw) {
	return (raw * kInv2PiFx + kFxHalf) >> 16;
}

} // namespace detail

/// `sin` de `q12`: ángulo en radianes, resultado en 4.12 por la tabla del original.
template <typename P>
struct scalar_sin<Fixed<s16, 12, P>> {
	static constexpr Fixed<s16, 12, P> op(Fixed<s16, 12, P> x) {
		const s32 idx = detail::retro_sin_index(static_cast<s32>(x.v)) & detail::kRetroSinMask;
		return Fixed<s16, 12, P> {eng::retro::kSinQ12[idx]};
	}
};

/// `cos` de `q12`: misma tabla, desfase de un cuarto de vuelta.
template <typename P>
struct scalar_cos<Fixed<s16, 12, P>> {
	static constexpr Fixed<s16, 12, P> op(Fixed<s16, 12, P> x) {
		const s32 idx = (detail::retro_sin_index(static_cast<s32>(x.v)) + detail::kRetroSinQuarter) &
				detail::kRetroSinMask;
		return Fixed<s16, 12, P> {eng::retro::kSinQ12[idx]};
	}
};

/// `sin` y `cos` de `q12` en **una sola pasada**: índice una vez y dos lecturas de tabla.
template <typename P>
struct scalar_sincos<Fixed<s16, 12, P>> {
	static constexpr void op(Fixed<s16, 12, P> x, Fixed<s16, 12, P>& out_sin,
				 Fixed<s16, 12, P>& out_cos) {
		const s32 idx = detail::retro_sin_index(static_cast<s32>(x.v));
		out_sin = Fixed<s16, 12, P> {eng::retro::kSinQ12[idx & detail::kRetroSinMask]};
		out_cos = Fixed<s16, 12, P> {
			eng::retro::kSinQ12[(idx + detail::kRetroSinQuarter) & detail::kRetroSinMask]};
	}
};

} // namespace eng::math
