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
/// error es un mensaje, no un "no matching operator/".
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
///   `lerp`/`smoothstep` funcionan (suma y producto), `remap`/`inv_lerp` no compilan
///   (a propósito).

#include <eng/core/linalg.hpp>
#include <eng/core/numeric_traits.hpp>

namespace eng::math {

/// Recorta `x` a `[lo, hi]`.
template <typename S>
[[nodiscard]] constexpr S clamp(S x, S lo, S hi) {
	return x < lo ? lo : (x > hi ? hi : x);
}

/// Recorta `x` a `[0, 1]`.
template <typename S>
[[nodiscard]] constexpr S saturate(S x) {
	return clamp(x, scalar_traits<S>::zero(), scalar_traits<S>::one());
}

/// Interpolación lineal `a + (b-a)·t` (extrapola si `t` sale de `[0,1]`).
template <typename S>
[[nodiscard]] constexpr S lerp(S a, S b, S t) {
	return a + (b - a) * t;
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
	return t * t * (three - two * t);
}

/// Easing `6t⁵ - 15t⁴ + 10t³` sobre `t` saturado (derivada y curvatura 0 en extremos).
template <typename S>
[[nodiscard]] constexpr S smootherstep(S t) {
	t = saturate(t);
	const S six = scalar_traits<S>::from_int(6);
	const S ten = scalar_traits<S>::from_int(10);
	const S fifteen = scalar_traits<S>::from_int(15);
	return t * t * t * (t * (t * six - fifteen) + ten);
}

} // namespace eng::math
