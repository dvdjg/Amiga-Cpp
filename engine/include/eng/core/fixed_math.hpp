#pragma once

/// \file fixed_math.hpp
/// **Funciones matemáticas para `Fixed`** (`eng::math`): especializa los puntos de
/// extensión de `scalar_math.hpp` (`sin`, `cos`, `sqrt`, `exp2`, `log2`, `log`, `exp`,
/// `pow`) para el escalar de coma fija de 16 bits, de modo que los algoritmos genéricos
/// (easings `_sine`/`_expo`, `smooth_damp`, `length`/`normalize`, `stats::stddev`,
/// `pow`…) compilen también con fixed.
///
/// ## Tablas y precisión: se eligen en compilación
///
/// El **tamaño de las tablas** (resolución angular y uso de `.rodata`) y el **número de
/// términos de la serie de Taylor** son parámetros de plantilla; se elige uno u otro
/// según la precisión/coste que pida el uso. Los puntos de extensión `scalar_sin<S>`…
/// usan un tamaño por defecto, y las funciones `fixed_sin<E, Size, Iter>`,
/// `fixed_cos<…>`, `fixed_exp2<E, Size>` y `fixed_log2<E, Size>` permiten pedir otro.
///
/// | Seno `Size` | resolución angular | error típico (peor caso ≈ pendiente·paso) | `.rodata` (s16) |
/// |---|---|---|---|
/// | 4096 | 2π/4096 | ~1e-3 | 8 KiB |
/// | **1024 (defecto)** | 2π/1024 | ~5e-3 | 2 KiB |
/// | 256 | 2π/256 | ~2e-2 | 512 B |
///
/// `exp2`/`log2` usan **256 entradas `s32`** por defecto (1 KiB cada tabla); con `Size`
/// menor bajan proporcionalmente. `Iter` (términos de Taylor) solo afecta a la
/// **generación** de la tabla en compilación: con `Iter = 12` el error (≈1e-16) queda
/// por debajo del truncado de la tabla, así que bajarlo no cambia el runtime y solo
/// ahorra tiempo de compilación. Configuración mínima medida (`fixed_sin<12,256>` +
/// `fixed_exp2<12,64>` + `fixed_log2<12,64>`): **1 KiB** de `.rodata` en total.
///
/// Las tablas son `static constexpr` en structs de `detail`: se materializan **una vez**
/// por `(E, Size, Iter)` y se comparten entre funciones y entre TUs.
///
/// ## Límites
///
/// - `sin`/`cos` requieren `E <= 14` (amplitud `2^E` en `s16`).
/// - `exp2` **satura** al rango del fixed. `log2` solo es correcto si el resultado cabe
///   en `s16` (en q12, `x` en `[2^-8, 2^8)`); si no, satura. `pow(a,b) = 2^(b·log2 a)`
///   (a > 0). `sqrt` es `isqrt(raw << E)` (negativos → 0).
///
/// Incluir este header **antes** de usar las funciones; no lo arrastra `scalar_math.hpp`.

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

/// `log2(Size)` para `Size` potencia de dos.
template <int Size>
consteval int log2_size() {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla: Size potencia de dos");
	int e = 0;
	int s = Size;
	while (s > 1) {
		s >>= 1;
		++e;
	}
	return e;
}

/// Tabla de seno (muestras `s16`) para `Fixed<s16,E>`. `Iter` = términos de la serie.
template <int E, int Size = 1024, int Iter = 12>
struct FixedSineTable {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla de seno: Size potencia de dos");
	static_assert(Size % 4 == 0, "tabla de seno: Size divisible por 4 (cuadrante de cos)");
	static_assert(Iter >= 1, "tabla de seno: Iter >= 1");
	static constexpr int kSize = Size;
	static constexpr int kMask = Size - 1;
	static constexpr int kQuarter = Size / 4;
	/// Escala con 4 bits extra: `índice = (raw·kScale) >> (E+4)`.
	static constexpr int kScale = static_cast<int>(
		static_cast<double>(Size) * 16.0 / 6.283185307179586 + 0.5);
	static constexpr ct_array<s16, Size> value {[](usize i) -> s16 {
		const double rad = 6.283185307179586 * static_cast<double>(i) /
				   static_cast<double>(Size);
		return static_cast<s16>(eng::detail::sin_series<Iter>(rad) *
					static_cast<double>(1 << E));
	}};
};

/// Tabla de `2^f` (muestras `s32`) para `Fixed<s16,E>`.
template <int E, int Size = 256>
struct FixedExp2Table {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla exp2: Size potencia de dos");
	static_assert(Size <= 1024, "tabla exp2: Size razonable");
	static constexpr int kSize = Size;
	static constexpr ct_array<s32, Size> value {[](usize i) -> s32 {
		const double f = static_cast<double>(i) / static_cast<double>(Size);
		return static_cast<s32>(exp2_d(f) * static_cast<double>(1 << E) + 0.5);
	}};
};

/// Tabla de `log2(1+i/N)` (muestras `s32`) para `Fixed<s16,E>`.
template <int E, int Size = 256>
struct FixedLog2Table {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla log2: Size potencia de dos");
	static constexpr int kSize = Size;
	static constexpr ct_array<s32, Size> value {[](usize i) -> s32 {
		const double mv = 1.0 + static_cast<double>(i) / static_cast<double>(Size);
		return static_cast<s32>(log2_d(mv) * static_cast<double>(1 << E) + 0.5);
	}};
};

} // namespace detail

/// `sin` de `Fixed<s16,E>` por tabla. `Size`/`Iter` eligen precisión y coste.
template <int E, int Size = 1024, int Iter = 12, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_sin(Fixed<s16, E, P> x) {
	static_assert(E <= 14, "fixed_sin: 2^E debe caber en s16");
	using Tab = detail::FixedSineTable<E, Size, Iter>;
	const s32 idx = ((static_cast<s32>(x.v) * Tab::kScale) >> (E + 4)) & Tab::kMask;
	return Fixed<s16, E, P> {Tab::value.v[idx]};
}

/// `cos` de `Fixed<s16,E>` (misma tabla, desfase de un cuarto de vuelta).
template <int E, int Size = 1024, int Iter = 12, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_cos(Fixed<s16, E, P> x) {
	static_assert(E <= 14, "fixed_cos: 2^E debe caber en s16");
	using Tab = detail::FixedSineTable<E, Size, Iter>;
	const s32 idx = (((static_cast<s32>(x.v) * Tab::kScale) >> (E + 4)) + Tab::kQuarter) &
			Tab::kMask;
	return Fixed<s16, E, P> {Tab::value.v[idx]};
}

/// `2^x` de `Fixed<s16,E>` (`2^n · 2^f`, tabla + desplazamiento). Satura.
template <int E, int Size = 256, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_exp2(Fixed<s16, E, P> x) {
	using Tab = detail::FixedExp2Table<E, Size>;
	const s32 raw = static_cast<s32>(x.v);
	const s32 n = raw >> E;
	const s32 frac = raw - (n << E);
	s32 w = Tab::value.v[(frac * Tab::kSize) >> E];
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

/// `log2` de `Fixed<s16,E>` (exponente entero + tabla). Satura; `x <= 0` → 0.
template <int E, int Size = 256, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_log2(Fixed<s16, E, P> x) {
	if (x.v <= 0) {
		return Fixed<s16, E, P> {0};
	}
	const u32 v = static_cast<u32>(static_cast<u16>(x.v));
	int b = 0;
	while ((v >> (b + 1)) != 0u) {
		++b;
	}
	constexpr int kBits = detail::log2_size<Size>();
	const u32 frac = ((v - (1u << b)) << kBits) >> b;
	const s32 w = ((static_cast<s32>(b) - E) << E) +
		      detail::FixedLog2Table<E, Size>::value.v[frac];
	if (w > 32767) return Fixed<s16, E, P> {static_cast<s16>(32767)};
	if (w < -32768) return Fixed<s16, E, P> {static_cast<s16>(-32768)};
	return Fixed<s16, E, P> {static_cast<s16>(w)};
}

// --- Puntos de extensión (tamaño por defecto) ---------------------------------

/// `sin` de un `Fixed<s16,E>` por la tabla compartida (radianes en el propio fixed).
template <int E, typename P>
struct scalar_sin<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_sin(x); }
};

/// `cos` de un `Fixed<s16,E>` (misma tabla, desfase de un cuarto de vuelta).
template <int E, typename P>
struct scalar_cos<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_cos(x); }
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

/// `2^x` de un `Fixed<s16,E>`.
template <int E, typename P>
struct scalar_exp2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_exp2(x); }
};

/// `log2` de un `Fixed<s16,E>`.
template <int E, typename P>
struct scalar_log2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_log2(x); }
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
