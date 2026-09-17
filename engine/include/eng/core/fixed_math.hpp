#pragma once

/// \file fixed_math.hpp
/// **Funciones matemáticas para `Fixed`** (`eng::math`): especializa los puntos de
/// extensión de `scalar_math.hpp` (`scalar_sin`/`scalar_cos`/`scalar_sqrt`) para el
/// escalar de coma fija de 16 bits, de modo que los algoritmos genéricos (easings
/// `_sine`, `length`/`normalize`, `stats::stddev`…) compilen también con fixed.
///
/// Se separa de `scalar_math.hpp` para no cargar la tabla de seno ni `isqrt` en quien
/// no usa trigonometría fixed: basta con incluir este header **antes** de usar las
/// funciones (p. ej. antes de `interp.hpp` si se llama a `ease_in_sine<q12>`).
///
/// ## Implementación y límites
///
/// - **`sin`/`cos`**: tabla de seno `SineTable<(1<<E), 4096>` (generada en compile-time
///   por `sinetable.hpp`); el índice es `(raw·652) >> E` (convierte radianes a
///   vueltas/4096). Error de la tabla ≈ 1 ulp del escalar. Requiere `E <= 14` (la
///   amplitud `2^E` debe caber en `s16`).
/// - **`sqrt`**: `isqrt(raw << E)` (raíz entera de `sinetable`/`isqrt`), correcta para
///   `raw >= 0`; negativos devuelven 0. Precisión ≈ 1 ulp de la representación.
/// - `exp2` **no** se especializa para fixed (no es habitual y no hay tabla de exp).

#include <eng/core/fixed.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/sinetable.hpp>

namespace eng::math {

/// `sin` de un `Fixed<s16,E>` por tabla (radianes en el propio fixed).
template <int E, typename P>
struct scalar_sin<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		static_assert(E <= 14, "scalar_sin<Fixed>: 2^E debe caber en s16");
		constexpr s32 kAmp = 1 << E;
		constexpr SineTable<kAmp, 4096> kTab {};
		const s32 idx = ((static_cast<s32>(x.v) * 652) >> E) & 4095;
		return Fixed<s16, E, P> {static_cast<s16>(kTab.v[idx])};
	}
};

/// `cos` de un `Fixed<s16,E>` por la misma tabla (desfase de un cuarto de vuelta).
template <int E, typename P>
struct scalar_cos<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		static_assert(E <= 14, "scalar_cos<Fixed>: 2^E debe caber en s16");
		constexpr s32 kAmp = 1 << E;
		constexpr SineTable<kAmp, 4096> kTab {};
		const s32 idx = (((static_cast<s32>(x.v) * 652) >> E) + 1024) & 4095;
		return Fixed<s16, E, P> {static_cast<s16>(kTab.v[idx])};
	}
};

/// `sqrt` de un `Fixed<s16,E>` por `isqrt` (`raw << E` cabe en `u32` para `E <= 15`).
template <int E, typename P>
struct scalar_sqrt<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		static_assert(E <= 15, "scalar_sqrt<Fixed>: raw<<E debe caber en u32");
		if (x.v <= 0) {
			return Fixed<s16, E, P> {0};
		}
		const u32 scaled = static_cast<u32>(static_cast<u16>(x.v)) << E;
		return Fixed<s16, E, P> {static_cast<s16>(eng::isqrt(scaled))};
	}
};

} // namespace eng::math
