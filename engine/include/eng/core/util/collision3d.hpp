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
#include <eng/core/types/span.hpp>
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

/// **Heightfield** de alturas enteras en una rejilla regular (XZ). `heights` se
/// indexa como `z * width + x`; la altura se devuelve en unidades del escalar.
/// \tparam S escalar de la operacion.
template <class S>
struct Heightfield3D {
	eng::Span<const s16> heights {}; ///< alturas por celda.
	u16 width = 0u; ///< celdas en X.
	u16 depth = 0u; ///< celdas en Z.
	S origin_x {}; ///< X de la celda (0,0).
	S origin_z {}; ///< Z de la celda (0,0).
	S cell {}; ///< lado de celda.
	[[nodiscard]] bool valid() const noexcept {
		return heights.data() != nullptr && width != 0u && depth != 0u &&
		       heights.size() >= width * depth &&
		       cell > eng::math::scalar_traits<S>::zero();
	}
};

/// \return true si `(x,z)` cae dentro y deja la altura de la celda en `h`.
/// \tparam S escalar de la operacion.
template <class S>
[[nodiscard]] bool heightfield_at(const Heightfield3D<S>& field, S x, S z, S& h) {
	if (!field.valid()) return false;
	using T = eng::math::scalar_traits<S>;
	const S fx = eng::math::div_norm(x - field.origin_x, field.cell);
	const S fz = eng::math::div_norm(z - field.origin_z, field.cell);
	const s32 ix = T::to_int(fx);
	const s32 iz = T::to_int(fz);
	if (ix < 0 || iz < 0 || ix >= field.width || iz >= field.depth) {
		return false;
	}
	h = T::from_int(field.heights[iz * field.width + ix]);
	return true;
}

/// \return true si la esfera toca la altura del heightfield en su celda.
/// \param ground salida con la altura de la celda.
/// \tparam S escalar de la operacion.
template <class S>
[[nodiscard]] bool sphere_heightfield(const Sphere3<S>& sphere, const Heightfield3D<S>& field,
				      S& ground) {
	if (!heightfield_at(field, sphere.center.v[0], sphere.center.v[2], ground)) {
		return false;
	}
	return sphere.center.v[1] - sphere.radius <= ground;
}
/// **Sweep de una esfera contra una AABB** (un paso, sin tunneling): expande la
/// caja por el radio y lanza un rayo desde el centro con `delta` como direccion;
/// `hit.t` en [0,1] es la fraccion del desplazamiento hasta el contacto.
/// \tparam S escalar de la operacion.
template <class S>
[[nodiscard]] bool sweep_sphere_aabb(const Sphere3<S>& sphere, const eng::math::Vec<3, S>& delta,
				     const Aabb3<S>& box, RayHit3<S>& out) {
	Aabb3<S> expanded = box;
	for (int axis = 0; axis < 3; ++axis) {
		expanded.min.v[axis] = box.min.v[axis] - sphere.radius;
		expanded.max.v[axis] = box.max.v[axis] + sphere.radius;
	}
	const Ray3<S> ray {sphere.center, delta, eng::math::scalar_traits<S>::one()};
	return ray_aabb(ray, expanded, out);
}
/// Caja orientada: centro, tres ejes unitarios y semiejes.
/// \tparam S escalar de la operacion.
/// \tparam S escalar de la operacion.
template <class S>
struct Obb3 {
	eng::math::Vec<3, S> center {}; ///< centro world.
	eng::math::Vec<3, S> axis[3] {}; ///< ejes unitarios (columnas de la rotacion).
	eng::math::Vec<3, S> half {}; ///< semiejes a lo largo de axis[0..2].
};

/// \return radio de la OBB proyectado sobre `axis` (no normalizado).
/// \tparam S escalar de la operacion.
template <class S>
[[nodiscard]] constexpr S obb_radius(const Obb3<S>& o, const eng::math::Vec<3, S>& axis) {
	const S ax = eng::math::dot(axis, o.axis[0]);
	const S ay = eng::math::dot(axis, o.axis[1]);
	const S az = eng::math::dot(axis, o.axis[2]);
	const S rx = ax < eng::math::scalar_traits<S>::zero() ? -ax : ax;
	const S ry = ay < eng::math::scalar_traits<S>::zero() ? -ay : ay;
	const S rz = az < eng::math::scalar_traits<S>::zero() ? -az : az;
	return eng::math::mul_norm(rx, o.half.v[0]) + eng::math::mul_norm(ry, o.half.v[1]) +
	       eng::math::mul_norm(rz, o.half.v[2]);
}

/// \return true si dos OBB se solapan (SAT de 15 ejes: 3+3+9 cruces).
/// \tparam S escalar de la operacion.
template <class S>
[[nodiscard]] constexpr bool obb_overlap(const Obb3<S>& a, const Obb3<S>& b) {
	const eng::math::Vec<3, S> t = b.center - a.center;
	const auto separated = [&](const eng::math::Vec<3, S>& axis) -> bool {
		const S l2 = eng::math::dot(axis, axis);
		if (l2 == eng::math::scalar_traits<S>::zero()) return false;
		const S d = eng::math::dot(t, axis);
		const S ad = d < eng::math::scalar_traits<S>::zero() ? -d : d;
		return ad > obb_radius(a, axis) + obb_radius(b, axis);
	};
	for (u8 i = 0; i < 3u; ++i) {
		if (separated(a.axis[i]) || separated(b.axis[i])) return false;
	}
	for (u8 i = 0; i < 3u; ++i) {
		for (u8 j = 0; j < 3u; ++j) {
			const S c0 = eng::math::mul_norm(a.axis[i].v[1], b.axis[j].v[2]) -
				     eng::math::mul_norm(a.axis[i].v[2], b.axis[j].v[1]);
			const S c1 = eng::math::mul_norm(a.axis[i].v[2], b.axis[j].v[0]) -
				     eng::math::mul_norm(a.axis[i].v[0], b.axis[j].v[2]);
			const S c2 = eng::math::mul_norm(a.axis[i].v[0], b.axis[j].v[1]) -
				     eng::math::mul_norm(a.axis[i].v[1], b.axis[j].v[0]);
			const eng::math::Vec<3, S> cross {{c0, c1, c2}};
			if (separated(cross)) return false;
		}
	}
	return true;
}

/// Par de indices de una broadphase.
struct BroadphasePair {
	u16 a = 0u;
	u16 b = 0u;
};

/// **Sweep-and-prune 3D** sobre el eje X, capacidad fija. Pensado para AABBs
/// pequenos y N bajo (A500); evita el producto cartesiano completo.
/// \tparam S escalar de la operacion.
/// \tparam N numero maximo de cajas (>0).
template <class S, u16 N>
class SweepBroadphase3D {
	static_assert(N > 0u, "SweepBroadphase3D: N > 0");
	struct Entry {
		S min_x {};
		S max_x {};
		Aabb3<S> box {};
		u16 id = 0u;
	};
public:
	/// Anade una caja con identificador; false si no cabe.
	bool add(const Aabb3<S>& box, u16 id) noexcept {
		if (m_count >= N) return false;
		Entry& e = m_entry[m_count++];
		e.min_x = box.min.v[0];
		e.max_x = box.max.v[0];
		e.box = box;
		e.id = id;
		return true;
	}
	/// \return numero de pares solapados (se escriben hasta `out.size()`).
	u16 pairs(eng::Span<BroadphasePair> out) const noexcept {
		if (out.empty()) return 0u;
		Entry sorted[N];
		for (u16 i = 0u; i < m_count; ++i) sorted[i] = m_entry[i];
		for (u16 i = 1u; i < m_count; ++i) {
			const Entry key = sorted[i];
			u16 j = i;
			while (j > 0u && sorted[j - 1u].min_x > key.min_x) {
				sorted[j] = sorted[j - 1u];
				--j;
			}
			sorted[j] = key;
		}
		u16 count = 0u;
		for (u16 i = 0u; i < m_count; ++i) {
			for (u16 j = i + 1u; j < m_count; ++j) {
				if (sorted[j].min_x > sorted[i].max_x) break;
				if (aabb_overlap(sorted[i].box, sorted[j].box)) {
					if (count < out.size()) {
						out[count] = BroadphasePair {sorted[i].id, sorted[j].id};
					}
					++count;
				}
			}
		}
		return count;
	}
	void clear() noexcept { m_count = 0u; }

private:
	Entry m_entry[N] {};
	u16 m_count = 0u;
};
} // namespace eng::collision3d
