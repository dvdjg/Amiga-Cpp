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

#include <eng/core/math/interp.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/numeric_traits.hpp>

namespace eng::math {

/// **Bézier cuadrática** por de Casteljau (dos `lerp` anidados). Al no usar constantes
/// enteras, vale tal cual para `MiniFloat16` y `Fixed`; solo necesita `lerp`.
/// `always_inline`: son 6 `lerp` en la cúbica; fuera de línea sería un árbol de `jsr`.
template <typename S>
[[nodiscard, gnu::always_inline]] constexpr S bezier2(S p0, S p1, S p2, S t) {
	return lerp(lerp(p0, p1, t), lerp(p1, p2, t), t);
}

/// **Bézier cúbica** por de Casteljau (seis `lerp`); pasa por `p0` y `p3`, y queda
/// controlada por `p1`/`p2`. Pensada para trayectorias de objetos y rampas de cámara.
template <typename S>
[[nodiscard, gnu::always_inline]] constexpr S bezier3(S p0, S p1, S p2, S p3, S t) {
	const S a = lerp(p0, p1, t);
	const S b = lerp(p1, p2, t);
	const S c = lerp(p2, p3, t);
	return lerp(lerp(a, b, t), lerp(b, c, t), t);
}

/// Bézier cuadrática componente a componente sobre `Vec<N,S>`.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> bezier2(const Vec<N, S>& p0, const Vec<N, S>& p1,
					  const Vec<N, S>& p2, S t) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = bezier2(p0.v[i], p1.v[i], p2.v[i], t);
	return r;
}

/// Bézier cúbica componente a componente sobre `Vec<N,S>` (trayectoria 2D/3D).
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> bezier3(const Vec<N, S>& p0, const Vec<N, S>& p1,
					  const Vec<N, S>& p2, const Vec<N, S>& p3, S t) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = bezier3(p0.v[i], p1.v[i], p2.v[i], p3.v[i], t);
	return r;
}

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

/// Hermite componente a componente sobre `Vec<N,S>` (misma tangente `t` para los N ejes).
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> hermite(const Vec<N, S>& p0, const Vec<N, S>& m0,
					  const Vec<N, S>& p1, const Vec<N, S>& m1, S t) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i) r.v[i] = hermite(p0.v[i], m0.v[i], p1.v[i], m1.v[i], t);
	return r;
}

/// Catmull-Rom componente a componente sobre `Vec<N,S>` (trayectoria que pasa por `p1` y
/// `p2`). Pensada para rutas de cámara/objetos en 2D/3D.
template <int N, typename S>
[[nodiscard]] constexpr Vec<N, S> catmull_rom(const Vec<N, S>& p0, const Vec<N, S>& p1,
					      const Vec<N, S>& p2, const Vec<N, S>& p3, S t) {
	Vec<N, S> r {};
	for (int i = 0; i < N; ++i)
		r.v[i] = catmull_rom(p0.v[i], p1.v[i], p2.v[i], p3.v[i], t);
	return r;
}

} // namespace eng::math
