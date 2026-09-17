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

#include <eng/core/ct_array.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/sinetable.hpp>

namespace eng::math {

namespace detail {

/// `ln(y)` para `y` en `(0,2)` por serie de `atanh((y−1)/(y+1))` (convergencia rápida).
[[nodiscard]] constexpr double ln_small(double y) {
	const double z = (y - 1.0) / (y + 1.0);
	const double z2 = z * z;
	double term = z;
	double sum = 0.0;
	for (int k = 0; k < 40; ++k) {
		sum += term / static_cast<double>(2 * k + 1);
		term *= z2;
	}
	return 2.0 * sum;
}

[[nodiscard]] constexpr double log2_d(double y) {
	return ln_small(y) / 0.69314718055994530942;
}

} // namespace detail

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

/// `2^x` de un `Fixed<s16,E>`: `2^n · 2^f` con la parte fraccionaria en una tabla
/// generada en compile-time (1024 entradas) y `2^n` por desplazamiento. Satura al
/// rango del fixed.
template <int E, typename P>
struct scalar_exp2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		const s32 raw = static_cast<s32>(x.v);
		const s32 n = raw >> E;                    // floor(x)
		const s32 frac = raw - (n << E);           // [0, 2^E)
		constexpr int kBits = 10;
		constexpr int kSize = 1 << kBits;
		constexpr ct_array<s32, kSize> kTab {[](usize i) -> s32 {
			const double f = static_cast<double>(i) / static_cast<double>(kSize);
			return static_cast<s32>(detail::exp2_d(f) * static_cast<double>(1 << E) + 0.5);
		}};
		const s32 idx = (frac * kSize) >> E;
		s32 w = kTab.v[idx];
		if (n >= 0) {
			if (n >= 15) {
				w = 32767;
			} else {
				w <<= n;
			}
		} else {
			const s32 sh = -n;
			w = (sh >= 31) ? 0 : (w >> sh);
		}
		if (w > 32767) w = 32767;
		if (w < -32768) w = -32768;
		return Fixed<s16, E, P> {static_cast<s16>(w)};
	}
};

/// `log2` de un `Fixed<s16,E>`: exponente entero + tabla de `log2(1+i/1024)`. Solo es
/// válido si el resultado cabe en `s16` (p. ej. en q12, `x` en `[2^-8, 2^8)`); fuera de
/// ese rango **satura**.
template <int E, typename P>
struct scalar_log2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		if (x.v <= 0) {
			return Fixed<s16, E, P> {0}; // indefinido: se define 0
		}
		const u32 v = static_cast<u32>(static_cast<u16>(x.v));
		int b = 0;
		while ((v >> (b + 1)) != 0u) {
			++b; // floor(log2(v))
		}
		constexpr int kBits = 10;
		constexpr int kSize = 1 << kBits;
		constexpr ct_array<s32, kSize> kTab {[](usize i) -> s32 {
			const double mv = 1.0 + static_cast<double>(i) / static_cast<double>(kSize);
			return static_cast<s32>(detail::log2_d(mv) * static_cast<double>(1 << E) + 0.5);
		}};
		const u32 frac = ((v - (1u << b)) << kBits) >> b; // [0, 1024)
		const s32 w = ((static_cast<s32>(b) - E) << E) + kTab.v[frac];
		if (w > 32767) return Fixed<s16, E, P> {static_cast<s16>(32767)};
		if (w < -32768) return Fixed<s16, E, P> {static_cast<s16>(-32768)};
		return Fixed<s16, E, P> {static_cast<s16>(w)};
	}
};

} // namespace eng::math
