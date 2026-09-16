#pragma once

/// \file scalar_ops.hpp
/// **Primitivas genéricas de escalar**: `min`/`max`/`abs`/`sign` y `move_towards`. Son las
/// operaciones que aparecen en casi todo (IA, límites de cámara, velocidad, colisiones) y
/// que no necesitan ni división ni trascendentes: solo comparación y negación.
///
/// Sirven con **cualquier** escalar (`float`, `double`, `MiniFloat16`, `Fixed`): como no
/// usan `mul_norm`/`div_norm`, tampoco cambian de exponente ni saturan, y resultan
/// **exactas** para el escalar que las recibe (no introducen redondeo propio).
///
/// ```cpp
/// int v = eng::math::move_towards(px, target_x, speed); // homing de un disparo
/// const auto mx = eng::math::max(a, b);                 // límite de cámara
/// ```
///
/// ## Límites por escalar
///
/// - **`MiniFloat16`**: `abs`/`sign` preservan el signo de cero de la representación
///   (signo-magnitud); `min`/`max`/`move_towards` comparan, sin aritmética sorprendente.
/// - **`Fixed`**: `move_towards` suma/resta del mismo exponente (sin desbordar más que la
///   propia suma del escalar); con la política por defecto (`Saturate`) el resultado es
///   seguro.
/// - **`float`/`double`**: sin límites prácticos.

#include <eng/core/linalg.hpp>

namespace eng::math {

/// Menor de los dos (`b < a ? b : a`; usa solo `operator<`, que todo escalar define).
template <typename S>
[[nodiscard]] constexpr S min(S a, S b) {
	return b < a ? b : a;
}
/// Mayor de los dos.
template <typename S>
[[nodiscard]] constexpr S max(S a, S b) {
	return a < b ? b : a;
}
/// Valor absoluto (`-x` si `x < 0`). No usa `std::abs`: vale para MF y fixed.
template <typename S>
[[nodiscard]] constexpr S abs(S x) {
	return x < scalar_traits<S>::zero() ? -x : x;
}
/// Signo: `-1`, `0` o `+1` en el propio escalar `S`.
template <typename S>
[[nodiscard]] constexpr S sign(S x) {
	const S zero = scalar_traits<S>::zero();
	if (x < zero) return -scalar_traits<S>::one();
	return zero < x ? scalar_traits<S>::one() : zero;
}

/// Acerca `cur` a `target` como mucho `max_delta` (paso de velocidad/aceleración).
/// `max_delta` es **no negativo**; no usa `abs` para ahorrar un paso en 68000.
/// Si la distancia restante es menor que el paso, **clava** en `target` (evita el
/// "vibrado" clásico al pasarse de largo). Es el homing de IA y el límite de cámara.
template <typename S>
[[nodiscard]] constexpr S move_towards(S cur, S target, S max_delta) {
	if (target < cur) return (cur - target < max_delta) ? target : cur - max_delta;
	return (target - cur < max_delta) ? target : cur + max_delta;
}

} // namespace eng::math
