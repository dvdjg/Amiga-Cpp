#pragma once

/// \file interp.hpp
/// **Interpolación, easing y reescalado** genéricos: el MISMO código sirve para
/// `MiniFloat16`, `float`, `double` o un fixed, parametrizado por el escalar `S`. Es la
/// base de cámaras, scroll, HUD, spawns y animación (la conclusión del análisis de
/// funciones matemáticas: es lo que más "juego" aporta por línea de código).
///
/// Convenio de dominio: `t` en `[0,1]` (las funciones de easing lo saturan), `a`/`b` en
/// el mismo espacio que el resultado. Ningún algoritmo asume hardware.
///
/// Funciones que **dividen** (`inv_lerp`, `remap`) exigen `numeric_traits<S>::
/// has_division` y lo comprueban con `require_division`; con un fixed sin división el
/// error es un mensaje, no un "no matching operator/". `inv_lerp`/`remap` tienen
/// sobrecargas para `Fixed` que usan la división explícita `div_norm`.
///
/// Los easing **trigonométricos** (`_sine`/`_expo`) piden `sin`/`cos`/`exp2` al escalar
/// vía `scalar_math.hpp` (ADL o especialización); los **polinómicos** no necesitan nada
/// más que suma/producto, así que valen también para fixed.
///
/// ## Límites por escalar (importante)
///
/// - **`MiniFloat16`** (~10 bits de mantisa, rango `[2^-14, 65504]`):
///   - `lerp(a,b,t) = a + (b-a)·t` pierde el incremento si `|b-a|` es mucho menor que
///     `|a|` (early-out de exponente `>= 11`, ≈ `|b-a| < |a|/2048`): interpolar entre
///     dos valores de magnitud muy distinta y `t` pequeño da como resultado `a`.
///   - `remap`/`inv_lerp` usan la división del tipo (tabla de recíprocos, ~1e-3); si el
///     resultado sale de `[2^-14, 65504]` **satura** (0 o ∞), no envuelve.
///   - `smoothstep`/`smootherstep` trabajan en `[0,1]`: seguros (sin división).
/// - **`float`/`double`**: sin límites prácticos para estos usos.
/// - **`Fixed`**: representan fracciones, pero no tienen `operator/` en el núcleo;
///   `lerp`/`smoothstep`/easing polinómico funcionan (suma y producto), `remap`/
///   `inv_lerp` usan `div_norm`, y los easing trigonométricos no compilan (sin `sin`).

#include <eng/core/linalg.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/core/scalar_math.hpp>

namespace eng::math {

/// Recorta `x` a `[lo, hi]`. Solo usa `<` (que todo escalar define), no `>`.
template <typename S>
[[nodiscard]] constexpr S clamp(S x, S lo, S hi) {
	return x < lo ? lo : (hi < x ? hi : x);
}

/// Recorta `x` a `[0, 1]`.
template <typename S>
[[nodiscard]] constexpr S saturate(S x) {
	return clamp(x, scalar_traits<S>::zero(), scalar_traits<S>::one());
}

/// Interpolación lineal `a + (b-a)·t` (extrapola si `t` sale de `[0,1]`). El producto se
/// normaliza con `mul_norm`, así que funciona con fixed (donde `b-a` y `t` son del mismo
/// exponente) y con `float`/`MiniFloat16`.
template <typename S>
[[nodiscard]] constexpr S lerp(S a, S b, S t) {
	return a + mul_norm(b - a, t);
}

/// Inverso de `lerp`: `t` tal que `lerp(a,b,t) == v`. Necesita división.
template <typename S>
[[nodiscard]] constexpr S inv_lerp(S a, S b, S v) {
	require_division<S>();
	return (v - a) / (b - a);
}

/// Reescala `v` del rango `[in_lo, in_hi]` al `[out_lo, out_hi]`. Necesita división.
template <typename S>
[[nodiscard]] constexpr S remap(S v, S in_lo, S in_hi, S out_lo, S out_hi) {
	require_division<S>();
	return out_lo + (v - in_lo) / (in_hi - in_lo) * (out_hi - out_lo);
}

/// `inv_lerp` para fixed: usa `div_norm` (división **explícita** y saturante) en vez de
/// `operator/`, que el núcleo no ofrece. Todos los argumentos con el mismo exponente.
template <int E, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> inv_lerp(Fixed<s16, E, P> a, Fixed<s16, E, P> b,
						  Fixed<s16, E, P> v) {
	return div_norm(v - a, b - a);
}

/// `remap` para fixed, por la misma vía explícita (`div_norm` + `mul_norm`).
template <int E, typename P>
[[nodiscard]] constexpr Fixed<s16, E, P> remap(Fixed<s16, E, P> v, Fixed<s16, E, P> in_lo,
					       Fixed<s16, E, P> in_hi, Fixed<s16, E, P> out_lo,
					       Fixed<s16, E, P> out_hi) {
	const Fixed<s16, E, P> t = div_norm(v - in_lo, in_hi - in_lo);
	return out_lo + mul_norm(t, out_hi - out_lo);
}

/// Escalón: 0 si `x < edge`, 1 si no.
template <typename S>
[[nodiscard]] constexpr S step(S edge, S x) {
	return x < edge ? scalar_traits<S>::zero() : scalar_traits<S>::one();
}

/// Easing suave `3t² - 2t³` sobre `t` saturado a `[0,1]` (derivada 0 en los extremos).
template <typename S>
[[nodiscard]] constexpr S smoothstep(S t) {
	t = saturate(t);
	const S two = scalar_traits<S>::from_int(2);
	const S three = scalar_traits<S>::from_int(3);
	const S t2 = mul_norm(t, t);
	return mul_norm(t2, three - mul_norm(two, t));
}

/// Easing `6t⁵ - 15t⁴ + 10t³` sobre `t` saturado (derivada y curvatura 0 en extremos).
/// Necesita representar el coeficiente 15: un fixed 4.12 (rango ±8) **no** puede y el
/// `require_range` lo avisa al compilar (usar `smoothstep` o un fixed con más rango).
template <typename S>
[[nodiscard]] constexpr S smootherstep(S t) {
	require_range<S, -15.0, 15.0>();
	t = saturate(t);
	const S six = scalar_traits<S>::from_int(6);
	const S ten = scalar_traits<S>::from_int(10);
	const S fifteen = scalar_traits<S>::from_int(15);
	const S t2 = mul_norm(t, t);
	const S t3 = mul_norm(t2, t);
	const S inner = (mul_norm(six, t2) - mul_norm(fifteen, t)) + ten;
	return mul_norm(t3, inner);
}

// ---------------------------------------------------------------------------
//  Easing polinómico (sin división: funciona también con fixed)
// ---------------------------------------------------------------------------

/// Cubo de `t` reutilizado por los easings cúbicos.
template <typename S>
[[nodiscard]] constexpr S ease_cube(S t) {
	return mul_norm(mul_norm(t, t), t);
}

/// Cuadrático de entrada: `t²` (arranca lento).
template <typename S>
[[nodiscard]] constexpr S ease_in_quad(S t) {
	t = saturate(t);
	return mul_norm(t, t);
}
/// Cuadrático de salida: `t·(2−t)` (frena al llegar).
template <typename S>
[[nodiscard]] constexpr S ease_out_quad(S t) {
	t = saturate(t);
	return mul_norm(t, scalar_traits<S>::from_int(2) - t);
}
/// Cuadrático de entrada/salida (`2t²` y `1−2(1−t)²` en las mitades).
template <typename S>
[[nodiscard]] constexpr S ease_in_out_quad(S t) {
	t = saturate(t);
	const S one = scalar_traits<S>::one();
	const S two = scalar_traits<S>::from_int(2);
	if (mul_norm(two, t) < one) return mul_norm(two, mul_norm(t, t)); // 2t²
	const S u = one - t;
	return one - mul_norm(two, mul_norm(u, u)); // 1 − 2(1−t)²
}

/// Cúbico de entrada: `t³`.
template <typename S>
[[nodiscard]] constexpr S ease_in_cubic(S t) {
	t = saturate(t);
	return ease_cube(t);
}
/// Cúbico de salida: `1−((1−t)³)`.
template <typename S>
[[nodiscard]] constexpr S ease_out_cubic(S t) {
	t = saturate(t);
	return scalar_traits<S>::one() - ease_cube(scalar_traits<S>::one() - t);
}
/// Cúbico de entrada/salida (`4t³` y `1−4(1−t)³` en las mitades).
template <typename S>
[[nodiscard]] constexpr S ease_in_out_cubic(S t) {
	t = saturate(t);
	const S one = scalar_traits<S>::one();
	const S two = scalar_traits<S>::from_int(2);
	const S four = scalar_traits<S>::from_int(4);
	if (mul_norm(two, t) < one) return mul_norm(four, ease_cube(t)); // 4t³
	return one - mul_norm(four, ease_cube(one - t)); // 1 − 4(1−t)³
}

// ---------------------------------------------------------------------------
//  Easing trigonométrico (necesita sin/cos/exp2 del escalar: `scalar_math.hpp`)
// ---------------------------------------------------------------------------

/// Constantes de los easings trigonométricos.
namespace easeconst {
constexpr double half_pi = 1.57079632679489661923;
constexpr double pi = 3.14159265358979323846;
} // namespace easeconst

/// Senoidal de entrada: `1 − cos(t·π/2)`.
template <typename S>
[[nodiscard]] constexpr S ease_in_sine(S t) {
	t = saturate(t);
	return scalar_traits<S>::one() - scalar_cos<S>::op(t * scalar_const<S>(easeconst::half_pi));
}
/// Senoidal de salida: `sin(t·π/2)`.
template <typename S>
[[nodiscard]] constexpr S ease_out_sine(S t) {
	t = saturate(t);
	return scalar_sin<S>::op(t * scalar_const<S>(easeconst::half_pi));
}
/// Senoidal de entrada/salida: `(1 − cos(π·t))/2`.
template <typename S>
[[nodiscard]] constexpr S ease_in_out_sine(S t) {
	t = saturate(t);
	return (scalar_traits<S>::one() - scalar_cos<S>::op(t * scalar_const<S>(easeconst::pi))) * S(0.5f);
}
/// Exponencial de entrada: `2^(10t−10)` (0 para `t=0`).
template <typename S>
[[nodiscard]] constexpr S ease_in_expo(S t) {
	t = saturate(t);
	if (t == scalar_traits<S>::zero()) return scalar_traits<S>::zero();
	return scalar_exp2<S>::op(t * S(10.0f) - S(10.0f));
}
/// Exponencial de salida: `1 − 2^(−10t)` (1 para `t=1`).
template <typename S>
[[nodiscard]] constexpr S ease_out_expo(S t) {
	t = saturate(t);
	if (t == scalar_traits<S>::one()) return scalar_traits<S>::one();
	return scalar_traits<S>::one() - scalar_exp2<S>::op(S(-10.0f) * t);
}
/// Exponencial de entrada/salida.
template <typename S>
[[nodiscard]] constexpr S ease_in_out_expo(S t) {
	t = saturate(t);
	const S one = scalar_traits<S>::one();
	if (t < S(0.5f)) return scalar_exp2<S>::op(t * S(20.0f) - S(10.0f)) * S(0.5f);
	if (t == one) return one;
	return one - scalar_exp2<S>::op(S(10.0f) - t * S(20.0f)) * S(0.5f);
}

} // namespace eng::math
