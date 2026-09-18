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

/// Representación sin signo del MISMO ancho que `R`: el producto `mulu` es unsigned
/// (`mulu.w` en 68000). Si `mulu` tomara `R` con signo, sign-extendería el operando y
/// daría un resultado distinto al del 68000 para valores con el bit alto puesto.
template <typename R>
struct arith_unsigned;
template <>
struct arith_unsigned<s16> {
	using type = u16;
};
template <>
struct arith_unsigned<s32> {
	using type = u32;
};

/// Implementación **por defecto** (portable). Un backend de CPU la especializa.
template <typename R>
struct arith {
	using s_wide = typename arith_wide<R>::signed_t;
	using u_wide = typename arith_wide<R>::unsigned_t;
	using u_repr = typename arith_unsigned<R>::type;

	[[nodiscard]] static constexpr s_wide mul(R a, R b) { return static_cast<s_wide>(a) * b; }
	[[nodiscard]] static constexpr u_wide mulu(u_repr a, u_repr b) {
		return static_cast<u_wide>(a) * b;
	}
	[[nodiscard]] static constexpr R div(s_wide a, R b) { return static_cast<R>(a / b); }
};

/// Producto **ensanchado genérico** `R × R -> doble ancho`, sin nombrar el ancho: con `R`
/// de 16 bits en 68000 es `muls.w` (`arith<R>`); con `R` de 32 bits usa el producto nativo
/// del target. Es el nombre portable de `mul16` para los algoritmos escalar-genéricos.
template <typename R>
[[nodiscard]] constexpr typename arith_wide<R>::signed_t mul_wide(R a, R b) {
	return arith<R>::mul(a, b);
}

/// Producto sin signo ensanchado (`mulu.w` con `R` de 16 bits). El parámetro `R` es la
/// representación **con signo** (el backend `arith<R>` indexa por ella) y **no se deduce**
/// de los operandos sin signo: hay que indicarlo (`mulu_wide<s16>(a, b)`). Para el uso
/// corriente con 16 bits está el atajo `mulu16`.
template <typename R>
[[nodiscard]] constexpr typename arith_wide<R>::unsigned_t mulu_wide(
	typename arith_unsigned<R>::type a, typename arith_unsigned<R>::type b) {
	return arith<R>::mulu(a, b);
}

/// Producto `u16 × u16 -> u32` sin signo (`mulu.w` en 68000). Atajo ergonómico de
/// `mulu_wide<s16>`: fija `R = s16` y acepta cualquier entero convertible a `u16`.
[[nodiscard]] constexpr u32 mulu16(u16 a, u16 b) { return mulu_wide<s16>(a, b); }

/// Cociente de un valor ensanchado por `R`, con el cociente en `R` (`divs.w` con 16 bits).
template <typename R>
[[nodiscard]] constexpr R div_wide(typename arith_wide<R>::signed_t a, R b) {
	return arith<R>::div(a, b);
}

} // namespace eng::math

// Selección del backend por CPU objetivo (no por máquina): un 68000 es un 68000, sea un
// Amiga, un Atari ST o un Mega Drive. Va aquí, y no en el núcleo del cálculo, para que la
// especialización exista ANTES de la primera instanciación de `arith`.
#if defined(__mc68000__)
#include <eng/cpu/m68k/arith.hpp>
#endif
