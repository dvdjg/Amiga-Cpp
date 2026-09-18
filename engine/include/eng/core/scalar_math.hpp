#pragma once

/// \file scalar_math.hpp
/// **Puntos de extensión de funciones matemáticas escalares** (`sqrt`, `sin`, `cos`,
/// `exp2`): el algoritmo genérico (p. ej. un easing o una spline) pide la función por
/// aquí y el escalar la aporta. Igual que `scalar_traits`/`numeric_traits`, permiten que
/// el MISMO código valga para `MiniFloat16`, `float`, `double`… sin que el cálculo
/// conozca el tipo.
///
/// La plantilla **primaria** usa ADL (`sqrt(x)`, `sin(x)`…), así que un escalar que
/// defina esas funciones —como `MiniFloat16` con `minifloat_math.hpp`— funciona sin más.
/// Para `float`/`double` se dan implementaciones `constexpr` **sin `libm`** (series +
/// reducción de rango), de modo que el runtime Amiga sigue siendo freestanding.
///
/// ## Límites
///
/// - `sin`/`cos` de `float`/`double`: reducción `n·(π/2)` por aritmética `double`; válidas
///   para argumentos moderados (`|x|` de pocos miles). El easing usa `|x| <= π`.
/// - `exp2` de `float`/`double`: `2^n` por multiplicaciones encadenadas; pensada para
///   argumentos pequeños (`|x| <= 64`). Para `MiniFloat16` sí es la versión completa del
///   núcleo Q1.14.

#include <eng/core/numeric_traits.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

namespace detail {

constexpr double k_pi = 3.14159265358979323846;
constexpr double k_half_pi = 1.57079632679489661923;
constexpr double k_ln2 = 0.69314718055994530942;

/// Raíz cuadrada `constexpr` sin `libm`: semilla `2^(e/2)` por manipulación del
/// exponente y Newton. Converge a la precisión del tipo en `Iter` iteraciones.
template <typename F, typename U, int MantBits, eng::u32 ExpMask, int ExpBias, int Iter>
[[nodiscard]] constexpr F sqrt_newton(F x) {
	if (x <= F(0)) return F(0);
	const U bits = __builtin_bit_cast(U, x);
	const int e = static_cast<int>((bits >> MantBits) & ExpMask) - ExpBias;
	const U seed_bits = static_cast<U>(ExpBias + e / 2) << MantBits;
	F y = __builtin_bit_cast(F, seed_bits);
	for (int i = 0; i < Iter; ++i) y = F(0.5) * (y + x / y);
	return y;
}

/// `sin(r)` para `|r| <= π/4` (Taylor; ~1e-16 en doble con 12 términos).
[[nodiscard]] constexpr double sin_core(double r) {
	const double r2 = r * r;
	double term = r;
	double sum = r;
	for (int k = 1; k < 12; ++k) {
		term *= -r2 / static_cast<double>((2 * k) * (2 * k + 1));
		sum += term;
	}
	return sum;
}
/// `cos(r)` para `|r| <= π/4`.
[[nodiscard]] constexpr double cos_core(double r) {
	const double r2 = r * r;
	double term = 1.0;
	double sum = 1.0;
	for (int k = 1; k < 12; ++k) {
		term *= -r2 / static_cast<double>((2 * k - 1) * (2 * k));
		sum += term;
	}
	return sum;
}
/// Reducción `x = n·(π/2) + r`, con `r` en `[-π/4, π/4]` y `n` entero.
[[nodiscard]] constexpr int reduce_pio2(double x, double& r) {
	const int n = static_cast<int>(x * (2.0 / k_pi) + (x >= 0.0 ? 0.5 : -0.5));
	r = x - static_cast<double>(n) * k_half_pi;
	return n;
}
[[nodiscard]] constexpr double sin_d(double x) {
	double r;
	const int q = reduce_pio2(x, r);
	switch (q & 3) {
	case 0: return sin_core(r);
	case 1: return cos_core(r);
	case 2: return -sin_core(r);
	default: return -cos_core(r);
	}
}
[[nodiscard]] constexpr double cos_d(double x) {
	double r;
	const int q = reduce_pio2(x, r);
	switch (q & 3) {
	case 0: return cos_core(r);
	case 1: return -sin_core(r);
	case 2: return -cos_core(r);
	default: return sin_core(r);
	}
}
/// `2^x = 2^n · 2^f` con `f` en `[0,1)` (Taylor de `exp(f·ln2)` y `2^n` por bucle).
[[nodiscard]] constexpr double exp2_d(double x) {
	int n = static_cast<int>(x);
	if (static_cast<double>(n) > x) --n;
	const double f = x - static_cast<double>(n);
	const double g = f * k_ln2;
	double term = 1.0;
	double sum = 1.0;
	for (int k = 1; k < 18; ++k) {
		term *= g / static_cast<double>(k);
		sum += term;
	}
	if (n >= 0) {
		for (int i = 0; i < n; ++i) sum *= 2.0;
	} else {
		for (int i = 0; i < -n; ++i) sum *= 0.5;
	}
	return sum;
}

} // namespace detail

/// `sqrt` del escalar. Por defecto ADL (un escalar que defina `sqrt(S)`, como
/// `MiniFloat16`, funciona tal cual).
template <typename S>
struct scalar_sqrt {
	static constexpr S op(S x) { return sqrt(x); }
};
template <>
struct scalar_sqrt<float> {
	static constexpr float op(float x) { return detail::sqrt_newton<float, eng::u32, 23, 0xFFu, 127, 5>(x); }
};
template <>
struct scalar_sqrt<double> {
	static constexpr double op(double x) {
		return detail::sqrt_newton<double, unsigned long long, 52, 0x7FFu, 1023, 8>(x);
	}
};

/// `sin` del escalar (por defecto ADL).
template <typename S>
struct scalar_sin {
	static constexpr S op(S x) { return sin(x); }
};
template <>
struct scalar_sin<float> {
	static constexpr float op(float x) { return static_cast<float>(detail::sin_d(static_cast<double>(x))); }
};
template <>
struct scalar_sin<double> {
	static constexpr double op(double x) { return detail::sin_d(x); }
};

/// `cos` del escalar (por defecto ADL).
template <typename S>
struct scalar_cos {
	static constexpr S op(S x) { return cos(x); }
};
template <>
struct scalar_cos<float> {
	static constexpr float op(float x) { return static_cast<float>(detail::cos_d(static_cast<double>(x))); }
};
template <>
struct scalar_cos<double> {
	static constexpr double op(double x) { return detail::cos_d(x); }
};

/// `2^x` del escalar (por defecto ADL).
template <typename S>
struct scalar_exp2 {
	static constexpr S op(S x) { return exp2(x); }
};
template <>
struct scalar_exp2<float> {
	static constexpr float op(float x) { return static_cast<float>(detail::exp2_d(static_cast<double>(x))); }
};
template <>
struct scalar_exp2<double> {
	static constexpr double op(double x) { return detail::exp2_d(x); }
};

/// `log2` del escalar (por defecto ADL). El núcleo no lo necesita; lo aportan los
/// escalares que lo definan (p. ej. `MiniFloat16`) o `fixed_math.hpp` para `Fixed`.
template <typename S>
struct scalar_log2 {
	static constexpr S op(S x) { return log2(x); }
};

/// `log` natural del escalar (por defecto ADL).
template <typename S>
struct scalar_log {
	static constexpr S op(S x) { return log(x); }
};

/// `exp` del escalar (por defecto ADL).
template <typename S>
struct scalar_exp {
	static constexpr S op(S x) { return exp(x); }
};

/// `pow(a, b)` del escalar (por defecto ADL).
template <typename S>
struct scalar_pow {
	static constexpr S op(S a, S b) { return pow(a, b); }
};

/// `tan` del escalar (por defecto ADL).
template <typename S>
struct scalar_tan {
	static constexpr S op(S x) { return tan(x); }
};

/// `atan2(y, x)` del escalar (por defecto ADL).
template <typename S>
struct scalar_atan2 {
	static constexpr S op(S y, S x) { return atan2(y, x); }
};

/// `asin` del escalar (por defecto ADL).
template <typename S>
struct scalar_asin {
	static constexpr S op(S x) { return asin(x); }
};

/// `acos` del escalar (por defecto ADL).
template <typename S>
struct scalar_acos {
	static constexpr S op(S x) { return acos(x); }
};

/// `sincos(x, s, c)` del escalar (por defecto ADL): calcula seno y coseno del mismo
/// ángulo en una sola pasada. Lo aportan los escalares que lo definan (p. ej.
/// `MiniFloat16`) o `fixed_math.hpp` para `Fixed`.
template <typename S>
struct scalar_sincos {
	static constexpr void op(S x, S& out_sin, S& out_cos) { sincos(x, out_sin, out_cos); }
};

/// Constante escalar desde un `double` de compilación, **sin ambigüedad**: para
/// `MiniFloat16` usa el constructor de `float` (su formato solo tiene 10 bits de mantisa) y
/// para `Fixed<R,E>` cuantiza a `E` bits fraccionarios (redondeo al más cercano). Un
/// `static_cast<S>` genérico no vale para ninguno de los dos: en MF ambigüea y en fixed
/// interpretaría el `double` como representación cruda. Es un punto de extensión (struct)
/// porque `Fixed` necesita una especialización **parcial** (los escalares de usuario solo
/// tienen que copiar el molde genérico si su `static_cast<double>` ya es correcto).
template <typename S>
struct scalar_const {
	static constexpr S from(double v) { return static_cast<S>(v); }
};
template <>
struct scalar_const<MiniFloat16> {
	static constexpr MiniFloat16 from(double v) { return MiniFloat16(static_cast<float>(v)); }
};
template <typename R, int E, typename P>
struct scalar_const<Fixed<R, E, P>> {
	static constexpr Fixed<R, E, P> from(double v) {
		const double scaled = v * static_cast<double>(1 << E);
		const double rounded = scaled < 0.0 ? scaled - 0.5 : scaled + 0.5;
		return Fixed<R, E, P> {static_cast<R>(static_cast<long>(rounded))};
	}
};

} // namespace eng::math
