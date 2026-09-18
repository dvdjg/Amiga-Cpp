#pragma once

/// \file fixed_math.hpp
/// **Funciones matemáticas para `Fixed`** (`eng::math`): especializa los puntos de
/// extensión de `scalar_math.hpp` (`sin`, `cos`, `sincos`, `tan`, `asin`, `acos`,
/// `atan2`, `sqrt`, `exp2`, `log2`, `log`, `exp`, `pow`) para el escalar de coma fija de
/// 16 bits, de modo que los algoritmos genéricos (easings `_sine`/`_expo`, `smooth_damp`,
/// `length`/`normalize`, `stats::stddev`, `pow`…) compilen también con fixed.
///
/// ## Tablas y precisión: se eligen en compilación
///
/// El **tamaño de las tablas** (resolución angular y uso de `.rodata`) y el **número de
/// términos de la serie de Taylor** son parámetros de plantilla; se elige uno u otro
/// según la precisión/coste que pida el uso. Los puntos de extensión `scalar_sin<S>`…
/// usan un tamaño por defecto, y las funciones `fixed_sin<E, Size, Iter>`, `fixed_cos<…>`,
/// `fixed_exp2<E, Size>`, `fixed_log2<E, Size>` y `fixed_atan2<E, Size>` permiten pedir otro.
///
/// El **tamaño por defecto se fija por compilación** con `-DENG_FIXED_SIN_SIZE=…`,
/// `-DENG_FIXED_EXP2_SIZE=…`, `-DENG_FIXED_LOG2_SIZE=…`, `-DENG_FIXED_ATAN_SIZE=…` y
/// `-DENG_FIXED_SIN_ITER=…` (ver bloque de configuración tras los `#include`); útil para
/// recortar `.rodata` en un juego con poca memoria.
///
/// | Seno `Size` | resolución angular | error típico (peor caso ≈ pendiente·paso) | `.rodata` (s16) |
/// |---|---|---|---|
/// | 4096 | 2π/4096 | ~1e-3 | 8 KiB |
/// | **1024 (defecto)** | 2π/1024 | ~5e-3 | 2 KiB |
/// | 256 | 2π/256 | ~2e-2 | 512 B |
///
/// `exp2`/`log2` usan **256 entradas `s32`** por defecto (1 KiB cada tabla) y la de
/// `atan` (para `atan2`/`asin`/`acos`) **257 entradas `s32`** (~1 KiB); con `Size` menor
/// bajan proporcionalmente. `Iter` (términos de Taylor) solo afecta a la
/// **generación** de la tabla en compilación: con `Iter = 12` el error (≈1e-16) queda
/// por debajo del truncado de la tabla, así que bajarlo no cambia el runtime y solo
/// ahorra tiempo de compilación. Medido con `size`: **~5 KiB** de tablas con los valores
/// por defecto y **~1.8 KiB** configurando `-DENG_FIXED_SIN_SIZE=512` y
/// `-DENG_FIXED_{EXP2,LOG2,ATAN}_SIZE=64`.
///
/// Las tablas son `static constexpr` en structs de `detail`: se materializan **una vez**
/// por `(E, Size, Iter)` y se comparten entre funciones y entre TUs.
///
/// ## Límites
///
/// - `sin`/`cos` requieren `E <= 14` (amplitud `2^E` en `s16`).
/// - `tan` = `sin/cos` (satura donde `cos ≈ 0`); `atan2` requiere `E <= 13` (π debe caber
///   en `s16`) y la tabla de `atan` cubre el primer octante; `asin`/`acos` se derivan de
///   `atan2` y `sqrt` (`|x| >= 1` satura a `±π/2` por el `sqrt` de un valor no positivo).
/// - `exp2` **satura** al rango del fixed. `log2` solo es correcto si el resultado cabe
///   en `s16` (en q12, `x` en `[2^-8, 2^8)`); si no, satura. `pow(a,b) = 2^(b·log2 a)`
///   (a > 0). `sqrt` es `isqrt(raw << E)` (negativos → 0).
///
/// Incluir este header **antes** de usar las funciones; no lo arrastra `scalar_math.hpp`.

#include <eng/core/ct_array.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/isqrt.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/scalar_math.hpp>
#include <eng/core/sinetable.hpp>

/// --- Configuración por defecto de las tablas ----------------------------------
/// El tamaño con el que se materializan las tablas de los puntos de extensión
/// (`scalar_sin<Fixed>`…) se fija aquí y se puede **cambiar por compilación**:
///
/// ```
/// g++ -DENG_FIXED_SIN_SIZE=512 -DENG_FIXED_EXP2_SIZE=64 -DENG_FIXED_LOG2_SIZE=64 ...
/// ```
///
/// `ENG_FIXED_SIN_ITER` = términos de la serie de Taylor (solo afecta al coste de
/// compilación, no al runtime). `ENG_FIXED_ATAN_SIZE` = tabla de `atan`/`atan2`.
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

/// Tabla de seno (muestras `s16`) para `Fixed<s16,E>`. `Iter` = términos de la serie.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER>
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
template <int E, int Size = ENG_FIXED_EXP2_SIZE>
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
template <int E, int Size = ENG_FIXED_LOG2_SIZE>
struct FixedLog2Table {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla log2: Size potencia de dos");
	static constexpr int kSize = Size;
	static constexpr ct_array<s32, Size> value {[](usize i) -> s32 {
		const double mv = 1.0 + static_cast<double>(i) / static_cast<double>(Size);
		return static_cast<s32>(log2_d(mv) * static_cast<double>(1 << E) + 0.5);
	}};
};

/// Tabla de `atan(z)` (muestras `s32`) para `z = i/Size` en `[0,1]`; `Size+1` muestras.
template <int E, int Size = ENG_FIXED_ATAN_SIZE>
struct FixedAtanTable {
	static_assert(Size > 0 && (Size & (Size - 1)) == 0, "tabla atan: Size potencia de dos");
	static constexpr int kSize = Size;
	static constexpr ct_array<s32, Size + 1> value {[](usize i) -> s32 {
		const double z = static_cast<double>(i) / static_cast<double>(Size);
		return static_cast<s32>(atan_d(z) * static_cast<double>(1 << E) + 0.5);
	}};
};

} // namespace detail

/// `sin` de `Fixed<s16,E>` por tabla. `Size`/`Iter` eligen precisión y coste.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_sin(Fixed<s16, E, P> x) {
	static_assert(E <= 14, "fixed_sin: 2^E debe caber en s16");
	using Tab = detail::FixedSineTable<E, Size, Iter>;
	const s32 idx = ((static_cast<s32>(x.v) * Tab::kScale) >> (E + 4)) & Tab::kMask;
	return Fixed<s16, E, P> {Tab::value.v[idx]};
}

/// `cos` de `Fixed<s16,E>` (misma tabla, desfase de un cuarto de vuelta).
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_cos(Fixed<s16, E, P> x) {
	static_assert(E <= 14, "fixed_cos: 2^E debe caber en s16");
	using Tab = detail::FixedSineTable<E, Size, Iter>;
	const s32 idx = (((static_cast<s32>(x.v) * Tab::kScale) >> (E + 4)) + Tab::kQuarter) &
			Tab::kMask;
	return Fixed<s16, E, P> {Tab::value.v[idx]};
}

/// `sin` y `cos` de `x` en **una sola pasada**: calcula el índice una vez y lee las dos
/// entradas de la tabla (el coseno es la misma tabla desplazada un cuarto de vuelta). Más
/// barato que `fixed_sin` seguido de `fixed_cos` cuando se necesitan ambos del mismo ángulo.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER, typename P>
constexpr void fixed_sincos(Fixed<s16, E, P> x, Fixed<s16, E, P>& out_sin,
			    Fixed<s16, E, P>& out_cos) {
	static_assert(E <= 14, "fixed_sincos: 2^E debe caber en s16");
	using Tab = detail::FixedSineTable<E, Size, Iter>;
	const s32 idx = ((static_cast<s32>(x.v) * Tab::kScale) >> (E + 4)) & Tab::kMask;
	out_sin = Fixed<s16, E, P> {Tab::value.v[idx]};
	out_cos = Fixed<s16, E, P> {Tab::value.v[(idx + Tab::kQuarter) & Tab::kMask]};
}

/// `2^x` de `Fixed<s16,E>` (`2^n · 2^f`, tabla + desplazamiento). Satura.
template <int E, int Size = ENG_FIXED_EXP2_SIZE, typename P>
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
template <int E, int Size = ENG_FIXED_LOG2_SIZE, typename P>
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

// --- Trigonometría inversa y tangente ------------------------------------------

/// `tan` de `Fixed<s16,E>` como `sin/cos`; satura donde `cos ≈ 0`.
template <int E, int Size = ENG_FIXED_SIN_SIZE, int Iter = ENG_FIXED_SIN_ITER, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_tan(Fixed<s16, E, P> x) {
	using F = Fixed<s16, E, P>;
	const F c = fixed_cos<E, Size, Iter>(x);
	if (c.v == 0) {
		return F {static_cast<s16>(fixed_sin<E, Size, Iter>(x).v < 0 ? -32768 : 32767)};
	}
	return div_norm(fixed_sin<E, Size, Iter>(x), c);
}

/// `atan2(y,x)` de `Fixed<s16,E>` por octantes + tabla de `atan` en `[0,1]`; resultado
/// en `[-π, π]`. Requiere `E <= 13` (π debe caber en `s16`).
template <int E, int Size = ENG_FIXED_ATAN_SIZE, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_atan2(Fixed<s16, E, P> y, Fixed<s16, E, P> x) {
	static_assert(E <= 13, "fixed_atan2: pi debe caber en s16");
	using F = Fixed<s16, E, P>;
	using Tab = detail::FixedAtanTable<E, Size>;
	if (x.v == 0 && y.v == 0) {
		return F {0};
	}
	const s32 ax = x.v < 0 ? -static_cast<s32>(x.v) : x.v;
	const s32 ay = y.v < 0 ? -static_cast<s32>(y.v) : y.v;
	const s32 lo = (ax < ay ? ax : ay) > 32767 ? 32767 : (ax < ay ? ax : ay);
	const s32 hi = (ax < ay ? ay : ax) > 32767 ? 32767 : (ax < ay ? ay : ax);
	const F ratio = div_norm(F {static_cast<s16>(lo)}, F {static_cast<s16>(hi)});
	const s32 idx = (static_cast<s32>(ratio.v) * Tab::kSize) >> E;
	s32 ang = Tab::value.v[idx];
	if (ay > ax) {
		ang = scalar_const<F>::from(1.57079632679489661923).v - ang;
	}
	if (x.v < 0) {
		ang = scalar_const<F>::from(3.14159265358979323846).v - ang;
	}
	if (y.v < 0) {
		ang = -ang;
	}
	if (ang > 32767) ang = 32767;
	if (ang < -32768) ang = -32768;
	return F {static_cast<s16>(ang)};
}

/// `asin` de `Fixed<s16,E>` = `atan2(x, sqrt(1−x²))` (`|x| >= 1` → `±π/2`).
template <int E, int Size = ENG_FIXED_ATAN_SIZE, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_asin(Fixed<s16, E, P> x) {
	using F = Fixed<s16, E, P>;
	const F r = scalar_sqrt<F>::op(scalar_traits<F>::one() - mul_norm(x, x));
	return fixed_atan2<E, Size>(x, r);
}

/// `acos` de `Fixed<s16,E>` = `atan2(sqrt(1−x²), x)`.
template <int E, int Size = ENG_FIXED_ATAN_SIZE, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> fixed_acos(Fixed<s16, E, P> x) {
	using F = Fixed<s16, E, P>;
	const F r = scalar_sqrt<F>::op(scalar_traits<F>::one() - mul_norm(x, x));
	return fixed_atan2<E, Size>(r, x);
}

// --- Ángulos: pliegue y diferencia ---------------------------------------------

/// Pliega un ángulo `Fixed<s16,E>` a `[-π, π]`: `x − 2π·round(x/2π)`. Sin tabla; útil
/// para mantener los ángulos en el dominio fiable de `fixed_sin`/`fixed_cos`. El
/// argumento debe caber en `s16` (no pliega ángulos fuera del rango del propio fixed).
template <int E, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> wrap_angle(Fixed<s16, E, P> x) {
	static_assert(E >= 1 && E <= 15, "wrap_angle: E en [1,15]");
	using F = Fixed<s16, E, P>;
	// round-to-nearest de x/2π en crudo: `(x·(1/2π) + 2^(2E-1)) >> 2E` (redondeo por
	// suelo, válido también para negativos).
	const s32 inv_raw = scalar_const<F>::from(0.15915494309189535).v;
	const s32 num = static_cast<s32>(x.v) * inv_raw;
	const int n = (num + (1 << (2 * E - 1))) >> (2 * E);
	return x - mul_norm(scalar_traits<F>::from_int(n), scalar_const<F>::from(6.283185307179586));
}

/// Diferencia angular mínima `a − b` plegada a `[-π, π]`.
template <int E, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> angle_diff(Fixed<s16, E, P> a, Fixed<s16, E, P> b) {
	return wrap_angle(a - b);
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

/// `sqrt` de un `Fixed<s32,E>` (host/32 bits): `raw << E` puede pasar de 32 bits, de ahí
/// `isqrt64`. **No disponible en m68k** (la aritmética de 64 bits son libcalls).
template <int E, typename P>
struct scalar_sqrt<Fixed<s32, E, P>> {
	using S = Fixed<s32, E, P>;
	[[nodiscard]] static constexpr S op(S x) {
#if defined(__m68k__)
		(void)x;
		static_assert(sizeof(S) == 0u,
			      "eng::math: sqrt de Fixed<s32,E> usaria libgcc de 64 bits en m68k; usa "
			      "Fixed<s16,E> o compila para host/32 bits nativo");
		return S {0};
#else
		if (x.v <= 0) {
			return S {0};
		}
		const eng::u64 scaled = static_cast<eng::u64>(x.v) << E;
		return S {static_cast<eng::s32>(isqrt64(scaled))};
#endif
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

/// `tan` de un `Fixed<s16,E>`.
template <int E, typename P>
struct scalar_tan<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_tan(x); }
};

/// `atan2(y,x)` de un `Fixed<s16,E>` (radianes).
template <int E, typename P>
struct scalar_atan2<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> y, Fixed<s16, E, P> x) {
		return fixed_atan2(y, x);
	}
};

/// `asin` de un `Fixed<s16,E>` (radianes).
template <int E, typename P>
struct scalar_asin<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_asin(x); }
};

/// `acos` de un `Fixed<s16,E>` (radianes).
template <int E, typename P>
struct scalar_acos<Fixed<s16, E, P>> {
	static constexpr Fixed<s16, E, P> op(Fixed<s16, E, P> x) { return fixed_acos(x); }
};

/// `sincos` de un `Fixed<s16,E>` (seno y coseno del mismo ángulo en una sola pasada).
template <int E, typename P>
struct scalar_sincos<Fixed<s16, E, P>> {
	static constexpr void op(Fixed<s16, E, P> x, Fixed<s16, E, P>& out_sin,
				 Fixed<s16, E, P>& out_cos) {
		fixed_sincos(x, out_sin, out_cos);
	}
};

} // namespace eng::math
