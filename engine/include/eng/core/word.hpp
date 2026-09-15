#pragma once

/// \file word.hpp
/// Operaciones de **palabra de 16 bits** ensanchadas (el `mul16`/`mulu16`/`div16` del
/// original): producto/cociente de dos enteros de 16 bits. La aritmética la aporta
/// `arith<s16>`, así que en 68000 son `muls.w`/`mulu.w`/`divs.w` nativos y en host una
/// multiplicación/división portable.
///
/// Viven en el núcleo (y no en `math2d`) porque no tienen nada de geometría: son la
/// aritmética de palabra que usan por igual el recorte 2D, la proyección y el sombreado.

#include <eng/core/arith.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

/// `a / b` con cociente de 16 bits (`divs.w`); el dividendo es de 32 bits.
[[nodiscard]] constexpr s16 div16(s32 a, s16 b) { return arith<s16>::div(a, b); }

/// `a * b` con signo, ensanchado a 32 bits (`muls.w`).
[[nodiscard]] constexpr s32 mul16(s16 a, s16 b) { return arith<s16>::mul(a, b); }

/// `a * b` sin signo, ensanchado a 32 bits (`mulu.w`).
[[nodiscard]] constexpr u32 mulu16(u16 a, u16 b) { return arith<s16>::mulu(a, b); }

} // namespace eng::math
