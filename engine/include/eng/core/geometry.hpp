#pragma once

/// \file geometry.hpp
/// **Vocabulario geométrico genérico** sobre `Vec<N,S>` (el álgebra de `linalg.hpp`): el
/// mismo código sirve para `MiniFloat16`, `float`, `double` o un fixed. Continúa el
/// criterio de `interp.hpp`: algoritmos independientes del tipo, con los límites de cada
/// escalar documentados y comprobados en compilación cuando se puede.
///
/// La raíz cuadrada es un **punto de extensión** (`scalar_sqrt<S>`): el escalar la aporta
/// (ADL) o el usuario la especializa. Aquí se dan implementaciones `constexpr` para
/// `float` y `double` (Newton con semilla por exponente, sin `libm`), porque `length` y
/// `normalize` las necesitan.
///
/// ## Límites por escalar (importante)
///
/// - **`MiniFloat16`** (~10 bits, `[2^-14, 65504]`):
///   - `length_sq`/`distance_sq`: la suma de cuadrados **desborda** si las componentes
///     pasan de ~255 (`255²·3 > 65504`). Normalizar vectores de magnitud grande requiere
///     escalar antes (o no usar MF para eso).
///   - `length` = `sqrt(length_sq)`: si `length_sq < 2^-14` (componentes < ~0.008)
///     bajoflow a 0 y `normalize` divide por cero → ∞. Para vectores muy cortos, reescala.
///   - `normalize`, `reflect`, `project`: usan división (tabla de recíprocos, ~1e-3); la
///     dirección resultante es buena a ~10 bits.
/// - **`float`/`double`**: sin límites prácticos para geometría normal.
/// - **`Fixed`**: no define `operator/` ni `sqrt`; `dot`/`cross2`/`perp`/`vlerp`/`rotate2`
///   funcionan (suma y producto), las que dividen o hacen `sqrt` no compilan.

#include <eng/core/linalg.hpp>
#include <eng/core/numeric_traits.hpp>

namespace eng::math {

namespace detail {

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

} // namespace detail

/// Punto de extensión: `sqrt` del escalar. Por defecto usa ADL (un escalar que defina
/// `sqrt(S)`, como `MiniFloat16` en `minifloat_math.hpp`, funciona sin más).
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

/// `v · v` (sin `sqrt`): barato y sin pérdida de rango en el producto.
template <int N, typename S>
[[nodiscard]] constexpr S length_sq(const Vec<N, S>& v) {
	return dot(v, v);
}

/// `|a - b|²`.
template <int N, typename S>
[[nodiscard]] constexpr S distance_sq(const Vec<N, S>& a, const Vec<N, S>& b) {
	return length_sq(a - b);
}

/// `|v|` (necesita `scalar_sqrt<S>`).
template <int N, typename S>
[[nodiscard]] constexpr S length(const Vec<N, S>& v) {
	return scalar_sqrt<S>::op(length_sq(v));
}

/// `|a - b|`.
template <int N, typename S>
[[nodiscard]] constexpr S distance(const Vec<N, S>& a, const Vec<N, S>& b) {
	return scalar_sqrt<S>::op(distance_sq(a, b));
}

/// `v·k` componente a componente.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> vscale(const Vec<N, S>& v, S k) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = v.v[i] * k;
	return r;
}

/// `v / |v|` (necesita división y `sqrt`). Si `|v| == 0`, devuelve `v` (cero).
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> normalize(const Vec<N, S>& v) {
	require_division<S>();
	const S len = length(v);
	if (len == scalar_traits<S>::zero()) return v;
	return vscale(v, scalar_traits<S>::one() / len);
}

/// Interpolación lineal componente a componente.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> vlerp(const Vec<N, S>& a, const Vec<N, S>& b, S t) {
	return a + vscale(b - a, t);
}

/// Recorta cada componente a `[lo, hi]`.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> clamp(const Vec<N, S>& v, S lo, S hi) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = v.v[i] < lo ? lo : (v.v[i] > hi ? hi : v.v[i]);
	return r;
}

/// Producto cruz 2D (escalar): positivo si `b` está a la izquierda de `a`.
template <typename S>
[[nodiscard]] constexpr S cross2(const Vec<2, S>& a, const Vec<2, S>& b) {
	return a.v[0] * b.v[1] - a.v[1] * b.v[0];
}

/// Perpendicular 2D (giro de 90°): `(-y, x)`.
template <typename S>
[[nodiscard]] constexpr Vec<2, S> perp(const Vec<2, S>& v) {
	return {{-v.v[1], v.v[0]}};
}

/// Rotación 2D por `(c, s)` = `(cos, sin)`.
template <typename S>
[[nodiscard]] constexpr Vec<2, S> rotate2(const Vec<2, S>& v, S c, S s) {
	return {{c * v.v[0] - s * v.v[1], s * v.v[0] + c * v.v[1]}};
}

/// Proyección de `v` sobre `onto`: `(v·onto / onto·onto)·onto`.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> project(const Vec<N, S>& v, const Vec<N, S>& onto) {
	require_division<S>();
	return vscale(onto, dot(v, onto) / dot(onto, onto));
}

/// Componente de `v` perpendicular a `onto`: `v - project(v, onto)`.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> reject(const Vec<N, S>& v, const Vec<N, S>& onto) {
	return v - project(v, onto);
}

/// Reflexión de `v` respecto a la normal `n` (unitaria): `v - 2(v·n)/|n|²·n`.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> reflect(const Vec<N, S>& v, const Vec<N, S>& n) {
	require_division<S>();
	const S two = scalar_traits<S>::from_int(2);
	return v - vscale(n, two * dot(v, n) / dot(n, n));
}

} // namespace eng::math
