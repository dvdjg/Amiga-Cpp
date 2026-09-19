#pragma once

/// \file arith.hpp
/// Especialización de `eng::math::arith` para el **procesador 68000**. Es del CPU, no
/// del chipset: sirve igual para Amiga, Atari ST o Mega Drive (todo lo que lleve un
/// 68000), así que vive en `eng/cpu/m68k/`, no en el backend de ninguna máquina.
///
/// Sólo cambia la IMPLEMENTACIÓN (instrucciones nativas de 16 bits); la semántica es la
/// del genérico y las pruebas de bit-exactitud son las mismas. `if consteval` mantiene la
/// función `constexpr` (el camino de compilación usa la multiplicación portable; en
/// runtime, `muls.w`/`mulu.w`/`divs.w` nativos, sin `__mulsi3`).

#include <eng/core/arith.hpp>

namespace eng::math {

template <>
struct arith<s16> {
	/// Producto **con signo** `s16 × s16 -> s32`. En runtime emite `muls.w` nativo (evita el
	/// libcall `__mulsi3`); en `consteval` usa la multiplicación portable para seguir siendo
	/// `constexpr`. Lo usan `fixed`/`linalg` a través de `mul_wide`/`arith<R>::mul`.
	[[nodiscard]] static constexpr s32 mul(s16 a, s16 b) {
		if consteval {
			return static_cast<s32>(a) * b;
		} else {
			s32 r;
			asm("muls %2,%0" : "=d"(r) : "0"(static_cast<s32>(a)), "dm"(b));
			return r;
		}
	}
	/// Producto **sin signo** `u16 × u16 -> u32` (`mulu.w`). Los operandos son `u16` (no
	/// `s16`) para NO sign-extender (ver `arith_unsigned<R>`); resultado exacto de 32 bits.
	[[nodiscard]] static constexpr u32 mulu(u16 a, u16 b) {
		if consteval {
			return static_cast<u32>(a) * b;
		} else {
			u32 r;
			asm("mulu %2,%0" : "=d"(r) : "0"(static_cast<u32>(a)), "dm"(b));
			return r;
		}
	}
	/// Cociente `s32 / s16 -> s16` (`divs.w`). En hardware es **saturante** ante overflow
	/// (no lanza); lo usa `div_wide`/`scalar_div` en el camino fixed.
	[[nodiscard]] static constexpr s16 div(s32 a, s16 b) {
		if consteval {
			return static_cast<s16>(a / b);
		} else {
			s16 r;
			asm("divs %2,%0" : "=d"(r) : "0"(a), "dm"(b));
			return r;
		}
	}
};

} // namespace eng::math
