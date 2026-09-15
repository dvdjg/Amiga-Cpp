#pragma once

/// \file arith.hpp
/// Aritmética entera **ensanchada** del núcleo: portable y sin nada de plataforma.
///
/// Es un punto de personalización: un backend de **CPU** puede especializar `arith<R>`
/// desde su propio fichero para usar instrucciones nativas (p. ej. `muls.w`/`divs.w` en
/// 68000, `muls` en 68020). El núcleo nunca incluye ese backend; lo hace la plataforma.
///
/// El formato/exponente NO vive aquí: eso es de `Fixed<Repr,Exp,Policy>`. Esto es sólo
/// "cómo multiplicar/dividir dos `R` de la forma más ancha y exacta posible".

#include <eng/core/types.hpp>

namespace eng::math {

/// Ancho del resultado de operar con dos `R` (con signo y sin signo).
template <typename R>
struct arith_wide;
template <>
struct arith_wide<s16> {
	using signed_t = s32;
	using unsigned_t = u32;
};
template <>
struct arith_wide<s32> {
	using signed_t = long long;
	using unsigned_t = unsigned long long;
};

/// Implementación **por defecto** (portable). Un backend de CPU la especializa.
template <typename R>
struct arith {
	using s_wide = typename arith_wide<R>::signed_t;
	using u_wide = typename arith_wide<R>::unsigned_t;

	[[nodiscard]] static constexpr s_wide mul(R a, R b) { return static_cast<s_wide>(a) * b; }
	[[nodiscard]] static constexpr u_wide mulu(R a, R b) { return static_cast<u_wide>(a) * b; }
	[[nodiscard]] static constexpr R div(s_wide a, R b) { return static_cast<R>(a / b); }
};

} // namespace eng::math

// Selección del backend por CPU objetivo (no por máquina): un 68000 es un 68000, sea un
// Amiga, un Atari ST o un Mega Drive. Va aquí, y no en el núcleo del cálculo, para que la
// especialización exista ANTES de la primera instanciación de `arith`.
#if defined(__mc68000__)
#include <eng/cpu/m68k/arith.hpp>
#endif
