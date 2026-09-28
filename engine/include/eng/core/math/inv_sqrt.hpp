#pragma once

/// \file inv_sqrt.hpp
/// **Tabla `1/√x` en punto fijo 0.16** (512 entradas, `u16`), generada en **compilación**
/// (`eng::ct_array`) — sin el literal de 512 valores y sin `sqrt` en runtime.
///
/// La usa el sombreado de luz (`eng/core/math/light.hpp`, `eng/cpu/m68k/light.hpp`): el producto
/// escalar normal·vista se eleva al cuadrado en la parte alta y se indexa aquí para obtener el
/// nivel de luz 0..15 **sin calcular `√` en el bucle del render**. Es el `65535/√x` precalculado
/// del lib3d original (demos 116/117).
///
/// Es **matemática pura** (ni hardware ni formato de fichero), por eso vive en `eng/core/math`
/// junto a `isqrt.hpp`, no en un backend; `lib3d.hpp` la re-exporta con `using`.
///
/// Generación y matiz: la tabla original de lib3d no sigue una fórmula perfecta (p. ej. `x=2`
/// trunca y `x=4` redondea). Aquí se genera con `⌊2⁴² / ⌊√x·2²⁶⌋⌋` **truncado** y saturado a 65535
/// (`[0] = 0`); la precisión del radical (2²⁶) hace despreciable el sesgo de la raíz entera, así
/// que reproduce `⌊65536/√x⌋` y solo difiere en ±1 en índices sueltos del original (aceptado).

#include <eng/core/data/ct_array.hpp>
#include <eng/core/types/types.hpp>

namespace eng::math {

/// Raíz cuadrada entera **exacta** (`constexpr`): mayor `r` tal que `r·r ≤ n` (bit a bit).
[[nodiscard]] constexpr eng::u64 isqrt_exact(eng::u64 n) noexcept {
	eng::u64 r = 0u;
	eng::u64 bit = 1ull << 62u;
	while (bit > n) {
		bit >>= 2u;
	}
	while (bit != 0u) {
		if (n >= r + bit) {
			n -= r + bit;
			r = (r >> 1u) + bit;
		} else {
			r >>= 1u;
		}
		bit >>= 2u;
	}
	return r;
}

/// La tabla estándar (512 entradas): `⌊2⁴² / ⌊√x·2²⁶⌋⌋`, saturada a 65535 (`[0] = 0`). El
/// estrechamiento a `u16` lo hace `ct_array` (tras la saturación el valor siempre cabe).
inline constexpr eng::ct_array<eng::u16, 512u> kInvSqrt {[](eng::usize x) -> eng::u64 {
	if (x == 0u) {
		return 0u;
	}
	const eng::u64 s = isqrt_exact(eng::u64{x} << 52u); // ⌊√x·2²⁶⌋ (≤ 511·2⁵² cabe en u64)
	const eng::u64 v = (1ull << 42u) / s;
	return v > 65535u ? 65535u : v;
}};

} // namespace eng::math
