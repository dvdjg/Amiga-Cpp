#pragma once

/// \file fixed_math.hpp
/// **Funciones matemáticas para `Fixed<Repr, E, P>`** (`eng::math`): especializa los
/// puntos de extensión de `scalar_math.hpp` (`sin`, `cos`, `sincos`, `tan`, `asin`,
/// `acos`, `atan2`, `sqrt`, `exp2`, `log2`, `log`, `exp`, `pow`) para el escalar de coma
/// fija, de modo que los algoritmos genéricos (easings `_sine`/`_expo`, `smooth_damp`,
/// `length`/`normalize`, `stats::stddev`, `pow`…) compilen también con fixed.
///
/// Está **parametrizado por la representación** `Repr` (`s16`, `s32`): el `s16` es el
/// camino retro (tablas `s16`, `muls.w`) y el `s32` usa la misma maquinaria con
/// acumuladores de doble ancho (**solo host/32 bits**: en m68k la aritmética de 64 bits
/// son libcalls y queda vetada con un `static_assert`).
///
/// ## Tablas y precisión: se eligen en compilación
///
/// El **tamaño de las tablas** (resolución angular y uso de `.rodata`) y el **número de
/// términos de la serie de Taylor** son parámetros de plantilla; los puntos de extensión
/// `scalar_sin<S>`… usan un tamaño por defecto, y las funciones `fixed_sin<R, E, Size, Iter>`,
/// `fixed_cos<R, …>`, `fixed_exp2<R, E, Size>`, `fixed_log2<R, E, Size>` y
/// `fixed_atan2<R, E, Size>` permiten pedir otro.
///
/// El **tamaño por defecto se fija por compilación** con `-DENG_FIXED_SIN_SIZE=…`,
/// `-DENG_FIXED_EXP2_SIZE=…`, `-DENG_FIXED_LOG2_SIZE=…`, `-DENG_FIXED_ATAN_SIZE=…` y
/// `-DENG_FIXED_SIN_ITER=…` (ver bloque de configuración tras los `#include`); útil para
/// recortar `.rodata` en un juego con poca memoria.
///
/// | Seno `Size` | resolución angular | error típico (peor caso ≈ pendiente·paso) | `.rodata` (`s16`) |
/// |---|---|---|---|
/// | 4096 | 2π/4096 | ~1e-3 | 8 KiB |
/// | **1024 (defecto)** | 2π/1024 | ~5e-3 | 2 KiB |
/// | 256 | 2π/256 | ~2e-2 | 512 B |
///
/// `exp2`/`log2` usan **256 entradas** por defecto (1 KiB cada tabla con muestras `s32`
/// cuando `Repr = s16`, que es su doble ancho) y la de `atan` (para `atan2`/`asin`/`acos`)
/// **257 entradas** (~1 KiB); con `Size` menor bajan proporcionalmente. Con `Repr = s32` las
/// muestras del seno pasan a `s32` y las de `exp2`/`log2`/`atan` a `s64`, de modo que
/// `.rodata` se **duplica**. `Iter` (términos de Taylor) solo afecta a la **generación** de
/// la tabla en compilación: con `Iter = 12` el error (≈1e-16) queda por debajo del truncado
/// de la tabla, así que bajarlo no cambia el runtime y solo ahorra tiempo de compilación.
/// Medido con `size`: **~5 KiB** de tablas con los valores por defecto (`s16`) y **~1.8 KiB**
/// configurando `-DENG_FIXED_SIN_SIZE=512` y `-DENG_FIXED_{EXP2,LOG2,ATAN}_SIZE=64`.
///
/// Las tablas son `static constexpr` en structs de `detail`: se materializan **una vez** por
/// `(R, E, Size, Iter)` y se comparten entre funciones y entre TUs.
///
/// ## Límites
///
/// - `sin`/`cos`/`sincos`: `E <= digits(Repr)-2` (`s16` → 14, `s32` → 30) para que la
///   amplitud `2^E` quepa en la representación.
/// - `atan2`/`asin`/`acos`: `E <= digits(Repr)-3` (π debe caber). `tan` = `sin/cos`
///   (satura donde `cos ≈ 0`).
/// - `exp2` **satura** al rango del fixed; `log2` satura si el resultado no cabe;
///   `pow(a,b) = 2^(b·log2 a)` (a > 0). `sqrt` es `isqrt(raw << E)` (negativos → 0).
///
/// Incluir este header **antes** de usar las funciones; no lo arrastra `scalar_math.hpp`.

#include <eng/core/data/ct_array.hpp>
#include <eng/core/math/fixed.hpp>
#include <eng/core/math/isqrt.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/numeric_traits.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/core/util/type_traits.hpp>

/// --- Configuración por defecto de las tablas ----------------------------------
#ifndef ENG_FIXED_SIN_SIZE
#define ENG_FIXED_SIN_SIZE 1024
#endif
#ifndef ENG_FIXED_SIN_ITER
#define ENG_FIXED_SIN_ITER 12
#endif
#ifndef ENG_FIXED_EXP2_SIZE
#define ENG_FIXED_EXP2_SIZE 256
#endif
#ifndef ENG_FIXED_LOG2_SIZE
#define ENG_FIXED_LOG2_SIZE 256
#endif
#ifndef ENG_FIXED_ATAN_SIZE
#define ENG_FIXED_ATAN_SIZE 256
#endif

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

/// Exponente máximo para el seno: `2^E` debe caber (deja signo y margen).
template <typename R>
[[nodiscard]] constexpr int fixed_max_sin_exp() noexcept {
	return static_cast<int>(sizeof(R) * 8u) - 2;
}
/// Exponente máximo con π representable (un bit menos que el seno).
template <typename R>
[[nodiscard]] constexpr int fixed_max_pi_exp() noexcept {
	return static_cast<int>(sizeof(R) * 8u) - 3;
}
/// Entero sin signo con el ancho del producto de `R` (para los índices de `log2`).
template <typename R>
using fixed_wide_unsigned = typename arith_wide<R>::unsigned_t;

/// Tabla de seno (muestras del propio `R`) para `Fixed<R,E>`.
template <typename R, int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER>
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
	static constexpr ct_array<R, Size> value {[](usize i) -> R {
		const double rad = 6.283185307179586 * static_cast<double>(i) /
				   static_cast<double>(Size);
		return static_cast<R>(eng::detail::sin_series<Iter>(rad) * pow2i(E));
	}};
};

/// Tabla de `2^f` (muestras de doble ancho) para `Fixed<R,E>`.
template <typename R, int E, int Size = ENG_FIXED_EXP2_SIZE>
struct FixedExp2Table {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla exp2: Size potencia de dos");
	static_assert(Size <= 1024, "tabla exp2: Size razonable");
	static constexpr int kSize = Size;
	static constexpr ct_array<typename wide<R>::type, Size> value {
		[](usize i) -> typename wide<R>::type {
			const double f = static_cast<double>(i) / static_cast<double>(Size);
			return static_cast<typename wide<R>::type>(exp2_d(f) * pow2i(E) + 0.5);
		}};
};

/// Tabla de `log2(1+i/N)` (muestras de doble ancho) para `Fixed<R,E>`.
template <typename R, int E, int Size = ENG_FIXED_LOG2_SIZE>
struct FixedLog2Table {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla log2: Size potencia de dos");
	static constexpr int kSize = Size;
	static constexpr ct_array<typename wide<R>::type, Size> value {
		[](usize i) -> typename wide<R>::type {
			const double mv = 1.0 + static_cast<double>(i) / static_cast<double>(Size);
			return static_cast<typename wide<R>::type>(log2_d(mv) * pow2i(E) + 0.5);
		}};
};

/// Tabla de `atan(z)` (muestras de doble ancho) para `z = i/Size` en `[0,1]`; `Size+1`.
template <typename R, int E, int Size = ENG_FIXED_ATAN_SIZE>
struct FixedAtanTable {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla atan: Size potencia de dos");
	static constexpr int kSize = Size;
	static constexpr ct_array<typename wide<R>::type, Size + 1> value {
		[](usize i) -> typename wide<R>::type {
			const double z = static_cast<double>(i) / static_cast<double>(Size);
			return static_cast<typename wide<R>::type>(atan_d(z) * pow2i(E) + 0.5);
		}};
};

} // namespace detail

/// `sin` de `Fixed<R,E>` por tabla. `Size`/`Iter` eligen precisión y coste.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER,
	  typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_sin(Fixed<R, E, P> x) {
	static_assert(E <= detail::fixed_max_sin_exp<R>(), "fixed_sin: 2^E debe caber en Repr");
	using W = typename wide<R>::type;
	using Tab = detail::FixedSineTable<R, E, Size, Iter>;
	const W idx = ((static_cast<W>(x.v) * static_cast<W>(Tab::kScale)) >> (E + 4)) & Tab::kMask;
	return Fixed<R, E, P> {static_cast<R>(Tab::value.v[idx])};
}

/// `cos` de `Fixed<R,E>` (misma tabla, desfase de un cuarto de vuelta).
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER,
	  typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_cos(Fixed<R, E, P> x) {
	static_assert(E <= detail::fixed_max_sin_exp<R>(), "fixed_cos: 2^E debe caber en Repr");
	using W = typename wide<R>::type;
	using Tab = detail::FixedSineTable<R, E, Size, Iter>;
	const W idx = (((static_cast<W>(x.v) * static_cast<W>(Tab::kScale)) >> (E + 4)) +
		       Tab::kQuarter) &
		      Tab::kMask;
	return Fixed<R, E, P> {static_cast<R>(Tab::value.v[idx])};
}

/// `sin` y `cos` de `x` en **una sola pasada**: índice una vez y dos lecturas de tabla.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER,
	  typename R, typename P>
constexpr void fixed_sincos(Fixed<R, E, P> x, Fixed<R, E, P>& out_sin,
			    Fixed<R, E, P>& out_cos) {
	static_assert(E <= detail::fixed_max_sin_exp<R>(), "fixed_sincos: 2^E debe caber en Repr");
	using W = typename wide<R>::type;
	using Tab = detail::FixedSineTable<R, E, Size, Iter>;
	const W idx = ((static_cast<W>(x.v) * static_cast<W>(Tab::kScale)) >> (E + 4)) & Tab::kMask;
	out_sin = Fixed<R, E, P> {static_cast<R>(Tab::value.v[idx])};
	out_cos = Fixed<R, E, P> {static_cast<R>(Tab::value.v[(idx + Tab::kQuarter) & Tab::kMask])};
}

/// `2^x` de `Fixed<R,E>` (`2^n · 2^f`, tabla + desplazamiento). Satura.
template <int E, int Size = ENG_FIXED_EXP2_SIZE, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_exp2(Fixed<R, E, P> x) {
	using W = typename wide<R>::type;
	using Tab = detail::FixedExp2Table<R, E, Size>;
	const W raw = static_cast<W>(x.v);
	const W n = raw >> E;
	const W frac = raw - (n << E);
	W w = static_cast<W>(Tab::value.v[(frac * static_cast<W>(Tab::kSize)) >> E]);
	constexpr int kDigits = static_cast<int>(sizeof(R) * 8u);
	if (n >= 0) {
		w = (n >= (kDigits - 1)) ? static_cast<W>(limits<R>::max)
					 : static_cast<W>(w << n);
	} else {
		const W sh = -n;
		w = (sh >= kDigits) ? W {0} : static_cast<W>(w >> sh);
	}
	if (w > static_cast<W>(limits<R>::max)) w = static_cast<W>(limits<R>::max);
	if (w < static_cast<W>(limits<R>::min)) w = static_cast<W>(limits<R>::min);
	return Fixed<R, E, P> {static_cast<R>(w)};
}

/// `log2` de `Fixed<R,E>` (exponente entero + tabla). Satura; `x <= 0` → 0.
template <int E, int Size = ENG_FIXED_LOG2_SIZE, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_log2(Fixed<R, E, P> x) {
	using W = typename wide<R>::type;
	using U = detail::fixed_wide_unsigned<R>;
	if (x.v <= 0) {
		return Fixed<R, E, P> {0};
	}
	const U v = static_cast<U>(x.v);
	int b = 0;
	while ((v >> static_cast<unsigned>(b + 1)) != 0u) {
		++b;
	}
	constexpr int kBits = detail::log2_size<Size>();
	const U frac = ((v - (static_cast<U>(1) << b)) << kBits) >> b;
	const W w = static_cast<W>((static_cast<W>(b - E) << E) +
				  static_cast<W>(detail::FixedLog2Table<R, E, Size>::value.v[frac]));
	if (w > static_cast<W>(limits<R>::max)) return Fixed<R, E, P> {limits<R>::max};
	if (w < static_cast<W>(limits<R>::min)) return Fixed<R, E, P> {limits<R>::min};
	return Fixed<R, E, P> {static_cast<R>(w)};
}

// --- Trigonometría inversa y tangente ------------------------------------------

/// `tan` de `Fixed<R,E>` como `sin/cos`; satura donde `cos ≈ 0`.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER,
	  typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_tan(Fixed<R, E, P> x) {
	using F = Fixed<R, E, P>;
	const F c = fixed_cos<E, Size, Iter>(x);
	if (c.v == 0) {
		return F {static_cast<R>(fixed_sin<E, Size, Iter>(x).v < 0 ? limits<R>::min
									   : limits<R>::max)};
	}
	return div_norm(fixed_sin<E, Size, Iter>(x), c);
}

/// `atan2(y,x)` de `Fixed<R,E>` por octantes + tabla de `atan` en `[0,1]`; resultado en
/// `[-π, π]`. Requiere `E <= digits(Repr)-3` (π debe caber).
template <int E, int Size = ENG_FIXED_ATAN_SIZE, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_atan2(Fixed<R, E, P> y, Fixed<R, E, P> x) {
	static_assert(E <= detail::fixed_max_pi_exp<R>(), "fixed_atan2: pi debe caber en Repr");
	using F = Fixed<R, E, P>;
	using W = typename wide<R>::type;
	using Tab = detail::FixedAtanTable<R, E, Size>;
	if (x.v == 0 && y.v == 0) {
		return F {0};
	}
	const W ax = x.v < 0 ? -static_cast<W>(x.v) : static_cast<W>(x.v);
	const W ay = y.v < 0 ? -static_cast<W>(y.v) : static_cast<W>(y.v);
	const W lo = (ax < ay ? ax : ay) > static_cast<W>(limits<R>::max)
			     ? static_cast<W>(limits<R>::max)
			     : (ax < ay ? ax : ay);
	const W hi = (ax < ay ? ay : ax) > static_cast<W>(limits<R>::max)
			     ? static_cast<W>(limits<R>::max)
			     : (ax < ay ? ay : ax);
	const F ratio = div_norm(F {static_cast<R>(lo)}, F {static_cast<R>(hi)});
	const W idx = (static_cast<W>(ratio.v) * static_cast<W>(Tab::kSize)) >> E;
	W ang = static_cast<W>(Tab::value.v[idx]);
	if (ay > ax) {
		ang = static_cast<W>(scalar_const<F>::from(1.57079632679489661923).v) - ang;
	}
	if (x.v < 0) {
		ang = static_cast<W>(scalar_const<F>::from(3.14159265358979323846).v) - ang;
	}
	if (y.v < 0) {
		ang = -ang;
	}
	if (ang > static_cast<W>(limits<R>::max)) ang = static_cast<W>(limits<R>::max);
	if (ang < static_cast<W>(limits<R>::min)) ang = static_cast<W>(limits<R>::min);
	return F {static_cast<R>(ang)};
}

/// `asin` de `Fixed<R,E>` = `atan2(x, sqrt(1−x²))` (`|x| >= 1` → `±π/2`).
template <int E, int Size = ENG_FIXED_ATAN_SIZE, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_asin(Fixed<R, E, P> x) {
	using F = Fixed<R, E, P>;
	const F r = scalar_sqrt<F>::op(scalar_traits<F>::one() - mul_norm(x, x));
	return fixed_atan2<E, Size>(x, r);
}

/// `acos` de `Fixed<R,E>` = `atan2(sqrt(1−x²), x)`.
template <int E, int Size = ENG_FIXED_ATAN_SIZE, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> fixed_acos(Fixed<R, E, P> x) {
	using F = Fixed<R, E, P>;
	const F r = scalar_sqrt<F>::op(scalar_traits<F>::one() - mul_norm(x, x));
	return fixed_atan2<E, Size>(r, x);
}

// --- Ángulos: pliegue y diferencia ---------------------------------------------

/// Pliega un ángulo `Fixed<R,E>` a `[-π, π]`: `x − 2π·round(x/2π)`. Sin tabla.
template <int E, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> wrap_angle(Fixed<R, E, P> x) {
	static_assert(E >= 1 && E <= static_cast<int>(sizeof(R) * 8u) - 1,
		      "wrap_angle: E fuera de rango para Repr");
	using F = Fixed<R, E, P>;
	using W = typename wide<R>::type;
	const W inv_raw = static_cast<W>(scalar_const<F>::from(0.15915494309189535).v);
	const W num = static_cast<W>(x.v) * inv_raw;
	const int n = static_cast<int>((num + (static_cast<W>(1) << (2 * E - 1))) >> (2 * E));
	return x - mul_norm(scalar_traits<F>::from_int(n),
			   scalar_const<F>::from(6.283185307179586));
}

/// Diferencia angular mínima `a − b` plegada a `[-π, π]`.
template <int E, typename R, typename P>
[[nodiscard]] constexpr Fixed<R, E, P> angle_diff(Fixed<R, E, P> a, Fixed<R, E, P> b) {
	return wrap_angle(a - b);
}

// --- Puntos de extensión (tamaño por defecto) ---------------------------------

#if !defined(__m68k__)
/// `isqrt` de 64 bits (constexpr, bit a bit). Solo lo usa el `sqrt` de 32 bits en host.
[[nodiscard]] constexpr eng::u64 isqrt64(eng::u64 n) noexcept;
#endif

/// `sqrt` de un `Fixed<R,E>`: `isqrt(raw << E)`; para `R` de 32 bits usa `isqrt64` (host).
template <typename R, int E, typename P>
struct scalar_sqrt<Fixed<R, E, P>> {
	using S = Fixed<R, E, P>;
	[[nodiscard]] static constexpr S op(S x) {
		if (x.v <= 0) {
			return S {0};
		}
		if constexpr (sizeof(R) <= 2u) {
			static_assert(E <= 15, "scalar_sqrt<Fixed>: raw<<E debe caber en u32");
			const u32 scaled = static_cast<u32>(static_cast<eng::u16>(x.v)) << E;
			return S {static_cast<R>(eng::isqrt(scaled))};
		} else {
#if defined(__m68k__)
			static_assert(sizeof(S) == 0u,
				      "eng::math: sqrt de Fixed<s32,E> usaria libgcc de 64 bits en "
				      "m68k; usa Fixed<s16,E> o compila para host/32 bits nativo");
			return S {0};
#else
			const eng::u64 scaled = static_cast<eng::u64>(x.v) << E;
			return S {static_cast<R>(isqrt64(scaled))};
#endif
		}
	}
};

#if !defined(__m68k__)
/// `isqrt` de 64 bits (constexpr, bit a bit). Solo lo usa el `sqrt` de 32 bits en host.
[[nodiscard]] constexpr eng::u64 isqrt64(eng::u64 n) noexcept {
	eng::u64 res = 0u;
	eng::u64 bit = static_cast<eng::u64>(1) << 62u;
	while (bit > n) {
		bit >>= 2u;
	}
	while (bit != 0u) {
		if (n >= res + bit) {
			n -= res + bit;
			res = (res >> 1u) + bit;
		} else {
			res >>= 1u;
		}
		bit >>= 2u;
	}
	return res;
}
#endif

/// `sin` de un `Fixed<R,E>` por la tabla compartida (radianes en el propio fixed).
template <typename R, int E, typename P>
struct scalar_sin<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_sin(x); }
};

/// `cos` de un `Fixed<R,E>` (misma tabla, desfase de un cuarto de vuelta).
template <typename R, int E, typename P>
struct scalar_cos<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_cos(x); }
};

/// `2^x` de un `Fixed<R,E>`.
template <typename R, int E, typename P>
struct scalar_exp2<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_exp2(x); }
};

/// `log2` de un `Fixed<R,E>`.
template <typename R, int E, typename P>
struct scalar_log2<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_log2(x); }
};

/// `log` natural de un `Fixed<R,E>`: `log2(x)·ln2`.
template <typename R, int E, typename P>
struct scalar_log<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) {
		return mul_norm(scalar_log2<Fixed<R, E, P>>::op(x),
				scalar_const<Fixed<R, E, P>>::from(detail::kLn2Fixed));
	}
};

/// `exp` de un `Fixed<R,E>`: `2^(x/ln2)`.
template <typename R, int E, typename P>
struct scalar_exp<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) {
		return scalar_exp2<Fixed<R, E, P>>::op(
			div_norm(x, scalar_const<Fixed<R, E, P>>::from(detail::kLn2Fixed)));
	}
};

/// `pow(a,b)` de `Fixed<R,E>`: `2^(b·log2 a)` (a > 0; si no, 0).
template <typename R, int E, typename P>
struct scalar_pow<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> a, Fixed<R, E, P> b) {
		if (a.v <= 0) {
			return Fixed<R, E, P> {0};
		}
		return scalar_exp2<Fixed<R, E, P>>::op(
			mul_norm(b, scalar_log2<Fixed<R, E, P>>::op(a)));
	}
};

/// `tan` de un `Fixed<R,E>`.
template <typename R, int E, typename P>
struct scalar_tan<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_tan(x); }
};

/// `atan2(y,x)` de un `Fixed<R,E>` (radianes).
template <typename R, int E, typename P>
struct scalar_atan2<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> y, Fixed<R, E, P> x) {
		return fixed_atan2(y, x);
	}
};

/// `asin` de un `Fixed<R,E>` (radianes).
template <typename R, int E, typename P>
struct scalar_asin<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_asin(x); }
};

/// `acos` de un `Fixed<R,E>` (radianes).
template <typename R, int E, typename P>
struct scalar_acos<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> op(Fixed<R, E, P> x) { return fixed_acos(x); }
};

/// `sincos` de un `Fixed<R,E>` (seno y coseno del mismo ángulo en una sola pasada).
template <typename R, int E, typename P>
struct scalar_sincos<Fixed<R, E, P>> {
	static constexpr void op(Fixed<R, E, P> x, Fixed<R, E, P>& out_sin,
				 Fixed<R, E, P>& out_cos) {
		fixed_sincos(x, out_sin, out_cos);
	}
};

} // namespace eng::math
