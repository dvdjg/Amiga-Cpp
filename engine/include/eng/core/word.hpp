#pragma once

/// \file word.hpp
/// **Atajos de palabra de 16 bits** de la aritmética ensanchada genérica: producto y cociente
/// de dos enteros de 16 bits. Delegan en `mul_wide<s16>` / `mulu_wide` / `div_wide` (ver
/// `arith.hpp`), así que en 68000 son `muls.w`/`mulu.w`/`divs.w` nativos y en host una
/// multiplicación/división portable. El código nuevo usa directamente los nombres genéricos.
///
/// Viven en el núcleo (y no en la capa retro 2D) porque no tienen nada de geometría: son la
/// aritmética de palabra que usan por igual el recorte 2D, la proyección y el sombreado.

#include <eng/core/arith.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

/// `a / b` con cociente de 16 bits (`divs.w`); el dividendo es de 32 bits.
[[nodiscard]] constexpr s16 div16(s32 a, s16 b) { return div_wide<s16>(a, b); }

/// `a * b` con signo, ensanchado a 32 bits (`muls.w`).
[[nodiscard]] constexpr s32 mul16(s16 a, s16 b) { return mul_wide(a, b); }

/// `a * b` sin signo, ensanchado a 32 bits (`mulu.w`).
[[nodiscard]] constexpr u32 mulu16(u16 a, u16 b) { return mulu_wide<s16>(a, b); }

} // namespace eng::math
