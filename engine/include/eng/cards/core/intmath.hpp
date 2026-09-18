#pragma once

/// \file intmath.hpp
/// Aritmética entera **sin libcalls** para los caminos calientes de `eng::cards`.
///
/// El 68000 no multiplica ni divide de 32 bits en hardware: `s32 * s32` y `s32 / s32`
/// se convierten en llamadas a libgcc (`__mulsi3`, `__divsi3`, ~150 ciclos). En equity,
/// reparto de botes y pot odds solo hacen falta cocientes y razones en por mil, así que:
///
/// - los productos 16×16 usan `eng::math::mulu16` (nativo `mulu.w`);
/// - la división `u32 / u16` usa **`divu.w` nativo** por dos mitades de 16 bits
///   (división larga en base 65536): dos instrucciones, sin bucle y sin libgcc.
///
/// Un bucle de resta-desplazamiento se descartó: GCC lo reconoce como división y lo
/// sustituye por `__udivsi3`/`__divsi3` (verificado con `tools/analyze/codegen-report.mjs`).
///
/// Todas las funciones son `constexpr`: en host se pliegan y en 68000 generan código
/// nativo. La sonda de codegen verifica que no aparece ninguna libcall.
///
/// Verificación: HOST-164/166 (corrección) y codegen-report (sin libgcc en 68000).

#include <eng/core/arith.hpp>
#include <eng/core/types.hpp>

namespace eng::cards {

/// División `u32 / u16` nativa del 68000 (`divu.w`): cociente y **resto en 16 bits**.
/// Precondición: cociente < 65536. En host se calcula con `/` y `%`.
/// `divu.w` deja el cociente en la palabra baja de Dn y el resto en la alta.
[[nodiscard]] constexpr u16 divu_w(u32 dividend, u16 divisor, u16& remainder) noexcept {
#if defined(__mc68000__)
	if consteval {
		remainder = static_cast<u16>(dividend % divisor);
		return static_cast<u16>(dividend / divisor);
	} else {
		u32 result = 0u;
		asm("divu %2,%0" : "=d"(result) : "0"(dividend), "dm"(divisor) : "cc");
		remainder = static_cast<u16>(result >> 16u);
		return static_cast<u16>(result & 0xffffu);
	}
#else
	remainder = static_cast<u16>(dividend % divisor);
	return static_cast<u16>(dividend / divisor);
#endif
}

/// División entera `numerator / divisor` por **mitades de 16 bits** (divu.w), con el
/// resto en `remainder`. Sin `__udivsi3`/`__divsi3`. `divisor == 0` devuelve 0.
/// Para divisores > 65535 (raro en el motor) reduce la escala de ambos; todos los
/// llamadores usan divisores pequeños (muestras, nº de ganadores, ciegas).
///
/// `noinline` deliberado: si GCC lo inlinea en un contexto donde conoce el rango de los
/// operandos, reconoce el patrón y lo sustituye por `__divsi3` de libgcc (verificado);
/// como función propia se compila a dos `divu.w` y no arrastra libgcc.
[[gnu::noinline]] [[nodiscard]] constexpr u32 divmod32(u32 numerator, u32 divisor,
                                                        u32& remainder) noexcept {
	remainder = 0u;
	if (divisor == 0u) {
		return 0u;
	}
	while (divisor > 0xffffu) {
		numerator >>= 1u;
		divisor >>= 1u;
	}
	u16 rem = 0u;
	const u16 high = divu_w(numerator >> 16u, static_cast<u16>(divisor), rem);
	const u16 low = divu_w((static_cast<u32>(rem) << 16u) | (numerator & 0xffffu),
	                       static_cast<u16>(divisor), rem);
	remainder = rem;
	return (static_cast<u32>(high) << 16u) | static_cast<u32>(low);
}

/// Cociente `numerator / divisor` (resto descartado).
[[nodiscard]] constexpr u32 div32(u32 numerator, u32 divisor) noexcept {
	u32 remainder = 0u;
	return divmod32(numerator, divisor, remainder);
}

/// `part * 1000` sin `__mulsi3` (1000 = 1024 - 16 - 8).
[[nodiscard]] constexpr u32 mul_1000(u32 part) noexcept {
	return (part << 10u) - (part << 4u) - (part << 3u);
}

/// `part / total` en por mil (`0..1000`). Reduce la escala si `total` no cabe en 16
/// bits, para que la división siga siendo un `divu.w` nativo. Precondición: `part <= total`.
[[nodiscard]] constexpr u16 permille_u32(u32 part, u32 total) noexcept {
	if (total == 0u) {
		return 0u;
	}
	while (total > 0xffffu) {
		part >>= 1u;
		total >>= 1u;
		if (total == 0u) {
			return 0u;
		}
	}
	u32 remainder = 0u;
	const u32 quotient = divmod32(mul_1000(part), total, remainder);
	return static_cast<u16>(quotient);
}

/// Equity multiway aproximada a partir del equity **heads-up** en por mil: modela
/// `P(ganar vs n) ≈ hu / (hu + (1-hu)·n)`. Se usa con la tabla preflop (que es HU).
[[nodiscard]] constexpr u16 multiway_from_heads_up(u16 hu_permille, u8 opponents) noexcept {
	if (opponents == 0u) {
		return 1000u;
	}
	const u32 win = eng::math::mulu16(hu_permille, 1000u);
	const u32 lose = 1000u - hu_permille;
	const u32 denom = static_cast<u32>(hu_permille) +
	                  eng::math::mulu16(static_cast<u16>(lose), static_cast<u16>(opponents));
	if (denom == 0u) {
		return 0u;
	}
	return static_cast<u16>(div32(win, denom));
}

} // namespace eng::cards
