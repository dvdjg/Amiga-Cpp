#pragma once

/// \file inv_sqrt.hpp
/// **Tabla `1/√x`** para un fixed **`R.E`** (entradas en el propio `R`), generada en **compilación**
/// con `eng::ct_array` — sin literal de valores y sin `sqrt` en runtime.
///
/// Genérica en `(R, E)` y tamaño `N`, como las tablas de `fixed_math.hpp`: sirve para **cualquier
/// combinación de fixed**, no solo 0.16. El valor por defecto (`kInvSqrt`, `u16`/0.16/512) es la
/// instancia histórica que consumen los efectos de luz: el producto escalar normal·vista se eleva
/// al cuadrado en la parte alta y se indexa aquí para obtener el nivel de luz 0..15 **sin `√` en el
/// bucle del render** (el `65535/√x` del lib3d original; demos 116/117).
///
/// Es **matemática pura** (ni hardware ni formato de fichero): vive en `eng/core/math` junto a
/// `isqrt.hpp`; `lib3d.hpp` re-exporta la instancia con `using`.
///
/// Generación y matiz: la tabla original no seguía una fórmula perfecta (`x=2` trunca, `x=4`
/// redondea). Se genera con `⌊2^(E+k) / ⌊√x·2^k⌋⌋` (`k=26` bits extra: el sesgo de la raíz entera
/// queda despreciable), saturada al máximo de `R`; reproduce `⌊2^E/√x⌋` salvo ±1 en índices sueltos
/// del original (aceptado).

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

/// Tabla `1/√x` en formato `R.E` (`x = 0..N-1`; `[0] = 0`). `R` debe ser **sin signo** (la tabla es
/// una magnitud); la saturación usa el máximo de `R`.
template <typename R, int E, eng::usize N = 512u>
struct InvSqrtTable {
	static_assert(E > 0 && E < 32, "inv_sqrt: exponente E fuera de rango");
	static_assert(N >= 1u, "inv_sqrt: N debe ser >= 1");
	/// Exponente (bits de fracción) de la tabla: el consumidor lo usa para normalizar (`>> kExponent`).
	static constexpr int kExponent = E;
	/// Bits extra del radical: precisión de `√x` (el sesgo de `√` entera se vuelve despreciable).
	static constexpr int kFrac = 26;
	static constexpr eng::u64 kScale = eng::u64{1} << (E + kFrac);
	static constexpr eng::u64 kMax = eng::u64{R(~R{0})}; // máximo representable en R (sin signo)

	/// Entradas materializadas en compilación por `ct_array` (índice = `x`); `k[x]` lee la tabla.
	static constexpr ct_array<R, N> value {[](eng::usize x) -> eng::u64 {
		if (x == 0u) {
			return 0u;
		}
		const eng::u64 s = isqrt_exact(eng::u64{x} << (2 * kFrac)); // ⌊√x·2^kFrac⌋
		const eng::u64 v = kScale / s;
		return v > kMax ? kMax : v;
	}};
};

/// Instancia histórica: `1/√x` en `u16` formato 0.16 (512 entradas), la que consumen la luz de
/// lib3d (`shade`) y las demos 116/117.
inline constexpr auto kInvSqrt = InvSqrtTable<eng::u16, 16, 512u>::value;

} // namespace eng::math
