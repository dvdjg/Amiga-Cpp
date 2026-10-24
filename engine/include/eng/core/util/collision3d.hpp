#pragma once

/// \file collision3d.hpp
/// **Colisión 3D mínima y genérica** para el primer corte de juego (G6 parcial de
/// `ROADMAP_3D_GAME_API.md`): esfera-esfera, esfera-AABB, AABB-AABB, rayo-AABB y
/// rayo-esfera. Sin heap, sin STL y con el escalar como parámetro (`S`): sirve
/// `float` en host y `Fixed`/`Q4.4`/`Q6.6` en 68000.
///
/// Las distancias se comparan al cuadrado cuando basta (`sin sqrt`); el rayo
/// esfera usa `scalar_sqrt<S>` y el consumidor debe incluir la especialización
/// del escalar (`fixed_math.hpp` para `Fixed`) como en el resto del engine.
/// Los **OBB/SAT 3D y la broadphase 3D** quedan para la siguiente pasada de G6:
/// este header no finge cubrirlos.
///
/// Convenio: `Ray3::dir` no tiene por qué ser unitario; `t` está en las mismas
/// unidades que `dir`. Un `max_t` negativo desactiva el límite.

#include <eng/core/math/geometry.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/types/types.hpp>

namespace eng::collision3d {

/// Esfera en el escalar `S`: centro y radio (mismas unidades).
/// \tparam S escalar de la operación.
template <class S>
struct Sphere3 {
	eng::math::Vec<3, S> center {}; ///< centro.
	S radius {}; ///< radio >= 0.
};

/// Caja axis-aligned inclusiva.
/// \tparam S escalar de la operación.
template <class S>
struct Aabb3 {
	eng::math::Vec<3, S> min {}; ///< esquina mínima.
	eng::math::Vec<3, S> max {}; ///< esquina máxima.
};

/// Rayo/segmento: `origin + t·dir`, con `t` en [0, max_t] si `max_t >= 0`.
/// \tparam S escalar de la operación.
template <class S>
struct Ray3 {
	eng::math::Vec<3, S> origin {}; ///< origen.
	eng::math::Vec<3, S> dir {}; ///< dirección (no necesariamente unitaria).
	S max_t {}; ///< t máximo; negativo = sin límite.
};

/// Resultado de una consulta de rayo.
/// \tparam S escalar de la operación.
template <class S>
struct RayHit3 {
	bool hit = false; ///< true si hay intersección válida.
	S t {}; ///< parámetro de la primera intersección.
	eng::math::Vec<3, S> point {}; ///< punto de impacto.
	eng::math::Vec<3, S> normal {}; ///< normal aproximada en el impacto.
};

/// ¿Se solapan dos esferas? Compara distancias al cuadrado (sin `sqrt`).
/// \tparam S escalar de la operación.
template <class S>
[[nodiscard]] constexpr bool sphere_overlap(const Sphere3<S>& a, const Sphere3<S>& b) {
	const S r = a.radius + b.radius;
	return eng::math::distance_sq(a.center, b.center) <= eng::math::mul_norm(r, r);
}

/// ¿Se solapa una esfera con una AABB? Punto más cercano + distancia².
/// \tparam S escalar de la operación.
template <class S>
[[nodiscard]] constexpr bool sphere_aabb(const Sphere3<S>& s, const Aabb3<S>& box) {
	eng::math::Vec<3, S> q = s.center;
	for (int i = 0; i < 3; ++i) {
		if (q.v[i] < box.min.v[i]) q.v[i] = box.min.v[i];
		if (q.v[i] > box.max.v[i]) q.v[i] = box.max.v[i];
	}
	return eng::math::distance_sq(q, s.center) <= eng::math::mul_norm(s.radius, s.radius);
}

/// ¿Se solapan dos AABB?
/// \tparam S escalar de la operación.
template <class S>
[[nodiscard]] constexpr bool aabb_overlap(const Aabb3<S>& a, const Aabb3<S>& b) {
	for (int i = 0; i < 3; ++i) {
		if (a.max.v[i] < b.min.v[i] || b.max.v[i] < a.min.v[i]) return false;
	}
	return true;
}

/// Rayo contra AABB por el método de **slabs** (sin `sqrt`).
/// \param ray rayo con `dir` no nulo.
/// \param box caja inclusiva.
/// \param out resultado rellenado si devuelve `true`.
/// \return `true` si el rayo toca la caja dentro de `[0, max_t]`.
/// \tparam S escalar de la operación.
template <class S>
[[nodiscard]] bool ray_aabb(const Ray3<S>& ray, const Aabb3<S>& box, RayHit3<S>& out) {
	using T = eng::math::scalar_traits<S>;
	out.hit = false;
	const S zero = T::zero();
	S tmin = zero;
	S tmax = ray.max_t;
	if (ray.max_t < zero) {
		// Sin límite explícito: usar un valor grande del escalar no es portable;
		// el llamador debe pasar un max_t finito cuando no quiera infinito.
		return false;
	}
	int axis = 0;
	for (int i = 0; i < 3; ++i) {
		const S o = ray.origin.v[i];
		const S d = ray.dir.v[i];
		if (d == zero) {
			if (o < box.min.v[i] || o > box.max.v[i]) return false;
			continue;
		}
		S t1 = eng::math::div_norm(box.min.v[i] - o, d);
		S t2 = eng::math::div_norm(box.max.v[i] - o, d);
		if (t1 > t2) {
			const S tmp = t1;
			t1 = t2;
			t2 = tmp;
		}
		if (t1 > tmin) {
			tmin = t1;
			axis = i;
		}
		if (t2 < tmax) tmax = t2;
		if (tmin > tmax) return false;
	}
	if (tmin < zero || tmin > ray.max_t) return false;
	out.hit = true;
	out.t = tmin;
	for (int i = 0; i < 3; ++i) out.point.v[i] = ray.origin.v[i] + eng::math::mul_norm(ray.dir.v[i], tmin);
	for (int i = 0; i < 3; ++i) out.normal.v[i] = (i == axis) ? T::one() : zero;
	if (axis >= 0 && ray.dir.v[axis] > zero) out.normal.v[axis] = -T::one();
	return true;
}

/// Rayo contra esfera. La ecuación cuadrática se evalúa en `S`; el test del
/// header la ejercita con escalares de rango suficiente. Devuelve la primera
/// intersección con `t >= 0` y `t <= max_t`.
/// \tparam S escalar de la operación.
template <class S>
[[nodiscard]] bool ray_sphere(const Ray3<S>& ray, const Sphere3<S>& sphere, RayHit3<S>& out) {
	using T = eng::math::scalar_traits<S>;
	const S zero = T::zero();
	const S two = T::from_int(2);
	const S four = T::from_int(4);
	if (ray.max_t < zero || sphere.radius <= zero) return false;
	const eng::math::Vec<3, S> oc = ray.origin - sphere.center;
	const S a = eng::math::dot(ray.dir, ray.dir);
	if (a == zero) return false;
	const S b = eng::math::mul_norm(two, eng::math::dot(oc, ray.dir));
	const S c = eng::math::dot(oc, oc) -
		    eng::math::mul_norm(sphere.radius, sphere.radius);
	const S disc = eng::math::mul_norm(b, b) -
		       eng::math::mul_norm(four, eng::math::mul_norm(a, c));
	if (disc < zero) return false;
	const S root = eng::math::scalar_sqrt<S>::op(disc);
	const S den = eng::math::mul_norm(two, a);
	S t = eng::math::div_norm(-b - root, den);
	if (t < zero) {
		t = eng::math::div_norm(-b + root, den);
	}
	if (t < zero || t > ray.max_t) return false;
	out.hit = true;
	out.t = t;
	for (int i = 0; i < 3; ++i) out.point.v[i] = ray.origin.v[i] + eng::math::mul_norm(ray.dir.v[i], t);
	const eng::math::Vec<3, S> n = out.point - sphere.center;
	for (int i = 0; i < 3; ++i) {
		out.normal.v[i] = eng::math::div_norm(n.v[i], sphere.radius);
	}
	return true;
}

} // namespace eng::collision3d
