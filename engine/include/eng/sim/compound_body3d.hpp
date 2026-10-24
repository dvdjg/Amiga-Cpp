#pragma once

/// \file compound_body3d.hpp
/// **Cuerpo compuesto de esferas** para personajes/props (G7): varias esferas
/// locales comparten un cuerpo rigido y aportan masa, centro de masas e inercia
/// diagonal. La ruta de colision usa las esferas en world; el integrador actual
/// es de traslacion (el solver angular completo queda para el siguiente corte).
///
/// Sin heap, sin STL y generico sobre el escalar `S`, igual que `physics3d.hpp`.
/// La masa de cada esfera puede venir del asset (`ModelPart::mass`) o calcularse
/// a partir de densidad y radio con una aproximacion de juego.

#include <eng/core/math/geometry.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/collision3d.hpp>

namespace eng::sim3d {

/// Cuerpo rigido con `MaxSpheres` esferas locales. La masa y la inercia se
/// calculan en `finalize()`; `inv_mass == 0` lo deja estatico.
/// \tparam S escalar de posicion/velocidad.
/// \tparam MaxSpheres capacidad de esferas (> 0).
template <class S, eng::u16 MaxSpheres>
struct CompoundBody3D {
	static_assert(MaxSpheres > 0u, "CompoundBody3D: MaxSpheres debe ser > 0");

	eng::math::Vec<3, S> position {}; ///< origen del cuerpo en world.
	eng::math::Vec<3, S> velocity {}; ///< velocidad lineal.
	eng::collision3d::Sphere3<S> local[MaxSpheres] {}; ///< esferas locales.
	eng::collision3d::Sphere3<S> world[MaxSpheres] {}; ///< esferas en world (cache).
	S sphere_mass[MaxSpheres] {}; ///< masa de cada esfera.
	eng::u16 count = 0u; ///< esferas activas.
	S mass = eng::math::scalar_traits<S>::zero(); ///< masa total.
	S inv_mass = eng::math::scalar_traits<S>::zero(); ///< 1/masa; 0 = estatico.
	eng::math::Vec<3, S> inertia {}; ///< inercia diagonal respecto al COM.
	eng::math::Vec<3, S> inv_inertia {}; ///< 1/inercia por eje (0 = bloqueado).
	bool active = true;
	bool sleeping = false;

	using T = eng::math::scalar_traits<S>;

	/// Vacia el cuerpo y sus propiedades derivadas.
	void clear() noexcept {
		count = 0u;
		mass = T::zero();
		inv_mass = T::zero();
		inertia = {};
		inv_inertia = {};
		active = true;
		sleeping = false;
	}

	/// A\u00f1ade una esfera local y su masa. Devuelve false si no cabe.
	bool add_sphere(const eng::math::Vec<3, S>& center, S radius, S sphere_m) noexcept {
		if (count >= MaxSpheres || !(radius > T::zero())) {
			return false;
		}
		local[count].center = center;
		local[count].radius = radius;
		sphere_mass[count] = sphere_m > T::zero() ? sphere_m : T::zero();
		++count;
		return true;
	}

	/// Calcula masa, centro de masas e inercia diagonal, desplaza las esferas
	/// locales al COM y actualiza world.
	void finalize() noexcept {
		mass = T::zero();
		for (eng::u16 i = 0u; i < count; ++i) {
			mass = mass + sphere_mass[i];
		}
		if (!(mass > T::zero())) {
			inv_mass = T::zero();
			inv_inertia = {};
			update_world();
			return;
		}
		inv_mass = eng::math::div_norm(T::one(), mass);
		eng::math::Vec<3, S> com {};
		for (eng::u16 i = 0u; i < count; ++i) {
			const S w = eng::math::div_norm(sphere_mass[i], mass);
			for (int k = 0; k < 3; ++k) {
				com.v[k] = com.v[k] + eng::math::mul_norm(local[i].center.v[k], w);
			}
		}
		for (eng::u16 i = 0u; i < count; ++i) {
			for (int k = 0; k < 3; ++k) {
				local[i].center.v[k] = local[i].center.v[k] - com.v[k];
			}
		}
		const S two_fifths = eng::math::div_norm(T::from_int(2), T::from_int(5));
		for (int k = 0; k < 3; ++k) {
			inertia.v[k] = T::zero();
		}
		for (eng::u16 i = 0u; i < count; ++i) {
			const S m = sphere_mass[i];
			const S r2 = eng::math::mul_norm(local[i].radius, local[i].radius);
			const S self = eng::math::mul_norm(two_fifths, eng::math::mul_norm(m, r2));
			inertia.v[0] = inertia.v[0] +
				       eng::math::mul_norm(m, eng::math::mul_norm(local[i].center.v[1],
										 local[i].center.v[1]) +
							  eng::math::mul_norm(local[i].center.v[2],
									      local[i].center.v[2])) +
				       self;
			inertia.v[1] = inertia.v[1] +
				       eng::math::mul_norm(m, eng::math::mul_norm(local[i].center.v[0],
										 local[i].center.v[0]) +
							  eng::math::mul_norm(local[i].center.v[2],
									      local[i].center.v[2])) +
				       self;
			inertia.v[2] = inertia.v[2] +
				       eng::math::mul_norm(m, eng::math::mul_norm(local[i].center.v[0],
										 local[i].center.v[0]) +
							  eng::math::mul_norm(local[i].center.v[1],
									      local[i].center.v[1])) +
				       self;
		}
		for (int k = 0; k < 3; ++k) {
			inv_inertia.v[k] = inertia.v[k] > T::zero()
						 ? eng::math::div_norm(T::one(), inertia.v[k])
						 : T::zero();
		}
		update_world();
	}

	/// Recalcula las esferas world a partir de `position` (traslacion).
	void update_world() noexcept {
		for (eng::u16 i = 0u; i < count; ++i) {
			world[i].radius = local[i].radius;
			for (int k = 0; k < 3; ++k) {
				world[i].center.v[k] = position.v[k] + local[i].center.v[k];
			}
		}
	}

	/// Integra traslacion con gravedad; no rota el cuerpo.
	void integrate(S dt, S gravity) noexcept {
		if (!active || sleeping || inv_mass == T::zero() || !(dt > T::zero())) {
			return;
		}
		velocity.v[1] = velocity.v[1] + eng::math::mul_norm(gravity, dt);
		for (int k = 0; k < 3; ++k) {
			position.v[k] = position.v[k] + eng::math::mul_norm(velocity.v[k], dt);
		}
		update_world();
	}

	/// \return true si alguna esfera del cuerpo A toca una de B.
	[[nodiscard]] bool overlaps(const CompoundBody3D& other) const noexcept {
		for (eng::u16 i = 0u; i < count; ++i) {
			for (eng::u16 j = 0u; j < other.count; ++j) {
				if (eng::collision3d::sphere_overlap(world[i], other.world[j])) {
					return true;
				}
			}
		}
		return false;
	}
};

} // namespace eng::sim3d
