#pragma once

/// \file spline.hpp
/// **Curvas spline genéricas** sobre el escalar `S` (o cualquier tipo de valor con
/// `+`/`-`/`*` y los rasgos): Hermite cúbica y Catmull-Rom. Sirven para rutas de cámara,
/// interpolación de claves, trayectorias de objetos y easing paramétrico.
///
/// Todas las operaciones usan `mul_norm` (producto normalizado al escalar), así que el
/// MISMO código vale para `float`, `MiniFloat16` y `Fixed` (`catmull_rom` necesita
/// además la división explícita `div_norm`, que el fixed 4.12/8.8 sí ofrece).
///
/// ## Límites por escalar
///
/// - **`MiniFloat16`** (~10 bits): el resultado de evaluar la spline se redondea ~1 ulp
///   por término; con tangentes grandes el error relativo crece. Coordenadas/valores
///   fuera de `[2^-14, 65504]` saturan.
/// - **`Fixed` 4.12**: `hermite` funciona con suma/producto; `catmull_rom` usa `div_norm`
///   (un `divs.w`) para el factor 1/2. Todas las entradas deben caber en ±8.
/// - **`float`/`double`**: sin límites prácticos.

#include <eng/core/linalg.hpp>
#include <eng/core/numeric_traits.hpp>

namespace eng::math {

/// Interpolación cúbica de **Hermite**: pasa por `p0` y `p1` con tangentes `m0`/`m1`.
/// Bases: `h00 = 2t³−3t²+1`, `h10 = t³−2t²+t`, `h01 = −2t³+3t²`, `h11 = t³−t²`.
template <typename S>
[[nodiscard]] constexpr S hermite(S p0, S m0, S p1, S m1, S t) {
	const S two = scalar_traits<S>::from_int(2);
	const S three = scalar_traits<S>::from_int(3);
	const S t2 = mul_norm(t, t);
	const S t3 = mul_norm(t2, t);
	const S h00 = mul_norm(two, t3) - mul_norm(three, t2) + scalar_traits<S>::one();
	const S h10 = (t3 - mul_norm(two, t2)) + t;
	const S h01 = mul_norm(three, t2) - mul_norm(two, t3);
	const S h11 = t3 - t2;
	return mul_norm(h00, p0) + mul_norm(h10, m0) + mul_norm(h01, p1) + mul_norm(h11, m1);
}

/// **Catmull-Rom** (interpolante, local): pasa por `p1` y `p2`, con tangentes
/// `(p2−p0)/2` y `(p3−p1)/2`. `t` en `[0,1]` recorre el tramo `p1→p2`.
template <typename S>
[[nodiscard]] constexpr S catmull_rom(S p0, S p1, S p2, S p3, S t) {
	const S half = div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(2));
	const S m0 = mul_norm(p2 - p0, half);
	const S m1 = mul_norm(p3 - p1, half);
	return hermite(p1, m0, p2, m1, t);
}

} // namespace eng::math
