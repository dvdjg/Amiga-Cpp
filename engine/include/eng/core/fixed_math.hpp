#pragma once

/// \file fixed_math.hpp
/// **Funciones matemáticas para `Fixed`** (`eng::math`): especializa los puntos de
/// extensión de `scalar_math.hpp` (`sin`, `cos`, `sqrt`, `exp2`, `log2`, `log`, `exp`,
/// `pow`) para el escalar de coma fija de 16 bits, de modo que los algoritmos genéricos
/// (easings `_sine`/`_expo`, `smooth_damp`, `length`/`normalize`, `stats::stddev`,
/// `pow`…) compilen también con fixed.
///
/// Se separa de `scalar_math.hpp` para no cargar las tablas en quien no usa matemáticas
/// fixed: basta con incluir este header **antes** de usar las funciones.
///
/// ## Implementación y límites
///
/// - **`sin`/`cos`**: tabla **compartida** de 1024 muestras `s16` (generada por
///   `sinetable.hpp` con la serie de Taylor); el índice es `(raw·163) >> E`. Requiere
///   `E <= 14` (amplitud `2^E` en `s16`). Error ≈ 1 ulp del escalar.
/// - **`exp2`**: `2^n · 2^f` con una tabla **compartida** de 256 entradas `s32` y `2^n`
///   por desplazamiento; **satura** al rango del fixed. `exp(x) = 2^(x/ln2)`.
/// - **`log2`**: exponente entero + tabla compartida de 256 entradas `s32`; solo es
///   correcto si el resultado cabe en `s16` (en q12, `x` en `[2^-8, 2^8)`); si no,
///   satura. `log(x) = log2(x)·ln2`. `pow(a,b) = 2^(b·log2 a)` (a > 0).
/// - **`sqrt`**: `isqrt(raw << E)`, correcta para `raw >= 0`; negativos devuelven 0.
///
/// Las tablas son `static constexpr` en structs de `detail`, así que se materializan
/// **una vez** por exponente y se comparten entre las funciones (y entre TUs). Tamaño
/// de `.rodata` por exponente: **2 KiB** (seno) + **1 KiB** (`exp2`) + **1 KiB**
/// (`log2`); medido con `size` sobre un TU que usa las cuatro funciones.

#include <eng/core/ct_array.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/sinetable.hpp>

namespace eng::math {

namespace detail {

constexpr double kLn2Fixed = 0.69314718055994530942;

/// `ln(y)` para `y` en `(0,2)` por serie de `atanh((y−1)/(y+1))`.
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
	return ln_small(y) / kLn2Fixed;
}

/// Tabla de seno compartida (1024 muestras) para `Fixed<s16,E>`.
template <int E>
struct FixedSineTable {
	static constexpr int kSize = 1024;
	static constexpr int kMask = kSize - 1;
	static constexpr int kQuarter = kSize / 4;
	static constexpr ct_array<s16, kSize> value {[](usize i) -> s16 {
		const double rad = 6.283185307179586 * static_cast<double>(i) /
				   static_cast<double>(kSize);
		return static_cast<s16>(eng::detail::sin_series(rad) *
					static_cast<double>(1 << E));
	}};
};

/// Tabla compartida de `2^f` (256 muestras) para `Fixed<s16,E>`.
template <int E>
struct FixedExp2Table {
	static constexpr int kSize = 256;
	static constexpr ct_array<s32, kSize> value {[](usize i) -> s32 {
		const double f = static_cast<double>(i) / static_cast<double>(kSize);
		return static_cast<s32>(exp2_d(f) * static_cast<double>(1 << E) + 0.5);
	}};
};

/// Tabla compartida de `log2(1+i/N)` (256 muestras) para `Fixed<s16,E>`.
template <int E>
struct FixedLog2Table {
	static constexpr int kSize = 256;
	static constexpr int kBits = 8;
	static constexpr ct_array<s32, kSize> value {[](usize i) -> s32 {
		const double mv = 1.0 + static_cast<double>(i) / static_cast<double>(kSize);
		return static_cast<s32>(log2_d(mv) * static_cast<double>(1 << E) + 0.5);
	}};
};

} // namespace detail

/// `sin` de un `Fixed<s16,E>` por la tabla compartida (radianes en el propio fixed).
/// `163 ≈ kSize/(2π)`; el índice ya está normalizado módulo la tabla.
template <int E, typename P>
struct scalar_sin<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		static_assert(E <= 14, "scalar_sin<Fixed>: 2^E debe caber en s16");
		const s32 idx = ((static_cast<s32>(x.v) * 163) >> E) & detail::FixedSineTable<E>::kMask;
		return Fixed<s16, E, P> {detail::FixedSineTable<E>::value.v[idx]};
	}
};

/// `cos` de un `Fixed<s16,E>` (misma tabla, desfase de un cuarto de vuelta).
template <int E, typename P>
struct scalar_cos<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		static_assert(E <= 14, "scalar_cos<Fixed>: 2^E debe caber en s16");
		const s32 idx = (((static_cast<s32>(x.v) * 163) >> E) +
				 detail::FixedSineTable<E>::kQuarter) &
				detail::FixedSineTable<E>::kMask;
		return Fixed<s16, E, P> {detail::FixedSineTable<E>::value.v[idx]};
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

/// `2^x` de un `Fixed<s16,E>`: `2^n · 2^f` con la tabla compartida y `2^n` por
/// desplazamiento. Satura al rango del fixed.
template <int E, typename P>
struct scalar_exp2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		const s32 raw = static_cast<s32>(x.v);
		const s32 n = raw >> E;          // floor(x)
		const s32 frac = raw - (n << E); // [0, 2^E)
		const s32 idx = (frac * detail::FixedExp2Table<E>::kSize) >> E;
		s32 w = detail::FixedExp2Table<E>::value.v[idx];
		if (n >= 0) {
			w = (n >= 15) ? 32767 : (w << n);
		} else {
			const s32 sh = -n;
			w = (sh >= 31) ? 0 : (w >> sh);
		}
		if (w > 32767) w = 32767;
		if (w < -32768) w = -32768;
		return Fixed<s16, E, P> {static_cast<s16>(w)};
	}
};

/// `log2` de un `Fixed<s16,E>`: exponente entero + tabla compartida. Solo es válido si
/// el resultado cabe en `s16`; si no, satura. `x <= 0` devuelve 0.
template <int E, typename P>
struct scalar_log2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		if (x.v <= 0) {
			return Fixed<s16, E, P> {0};
		}
		const u32 v = static_cast<u32>(static_cast<u16>(x.v));
		int b = 0;
		while ((v >> (b + 1)) != 0u) {
			++b; // floor(log2(v))
		}
		const u32 frac = ((v - (1u << b)) << detail::FixedLog2Table<E>::kBits) >> b;
		const s32 w = ((static_cast<s32>(b) - E) << E) +
			      detail::FixedLog2Table<E>::value.v[frac];
		if (w > 32767) return Fixed<s16, E, P> {static_cast<s16>(32767)};
		if (w < -32768) return Fixed<s16, E, P> {static_cast<s16>(-32768)};
		return Fixed<s16, E, P> {static_cast<s16>(w)};
	}
};

/// `log` natural de un `Fixed<s16,E>`: `log2(x)·ln2`.
template <int E, typename P>
struct scalar_log<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		return mul_norm(scalar_log2<Fixed<s16, E, P>>::op(x),
				scalar_const<Fixed<s16, E, P>>::from(detail::kLn2Fixed));
	}
};

/// `exp` de un `Fixed<s16,E>`: `2^(x/ln2)`.
template <int E, typename P>
struct scalar_exp<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) {
		return scalar_exp2<Fixed<s16, E, P>>::op(
			div_norm(x, scalar_const<Fixed<s16, E, P>>::from(detail::kLn2Fixed)));
	}
};

/// `pow(a,b)` de `Fixed<s16,E>`: `2^(b·log2 a)` (a > 0; si no, 0).
template <int E, typename P>
struct scalar_pow<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> a, Fixed<s16, E, P> b) {
		if (a.v <= 0) {
			return Fixed<s16, E, P> {0};
		}
		return scalar_exp2<Fixed<s16, E, P>>::op(
			mul_norm(b, scalar_log2<Fixed<s16, E, P>>::op(a)));
	}
};

} // namespace eng::math
