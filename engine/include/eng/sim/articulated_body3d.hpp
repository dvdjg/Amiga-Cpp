#pragma once

/// \file articulated_body3d.hpp
/// **Cuerpo articulado simple por position-based dynamics (PBD)**: partes con
/// padre, pivote local, masa, inercia y esferas. El paso integra traslacion y
/// resuelve joints de bola por correccion de posiciones (varias iteraciones).
/// Es un primer solver de personajes/props sin joints angulares completos:
/// conserva masa e inercia por parte y produce posiciones world de las esferas.

#include <eng/core/math/geometry.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/collision3d.hpp>

namespace eng::sim3d {

/// Cuerpo articulado de capacidad fija.
/// \tparam S escalar de fisica.
/// \tparam MaxParts numero maximo de partes (>0).
/// \tparam MaxSpheres numero maximo de esferas (>0).
template <class S, eng::u16 MaxParts, eng::u16 MaxSpheres>
struct ArticulatedBody3D {
	static_assert(MaxParts > 0u && MaxSpheres > 0u, "ArticulatedBody3D capacidades > 0");
	using T = eng::math::scalar_traits<S>;

	struct PartState {
		s16 parent = -1; ///< indice de padre; -1 = raiz.
		eng::math::Vec<3, S> pivot {}; ///< pivote local respecto al padre.
		eng::math::Vec<3, S> position {}; ///< posicion world del pivote.
		eng::math::Vec<3, S> previous {}; ///< posicion previa (Verlet/PBD).
		S inv_mass = T::zero(); ///< 1/masa; 0 = estatico.
		S mass = T::zero(); ///< masa de la parte.
		eng::math::Vec<3, S> inertia {}; ///< inercia diagonal local.
		u16 sphere_first = 0u; ///< primer indice de esfera.
		u16 sphere_count = 0u; ///< numero de esferas.
	};

	PartState parts[MaxParts] {};
	eng::collision3d::Sphere3<S> local[MaxSpheres] {};
	eng::collision3d::Sphere3<S> world[MaxSpheres] {};
	u16 part_count = 0u;
	u16 sphere_count = 0u;

	/// Vacia partes, esferas y estado derivado.
	void clear() noexcept {
		part_count = 0u;
		sphere_count = 0u;
	}

	/// Anade una parte; el padre debe ser anterior o -1.
	bool add_part(s16 parent, const eng::math::Vec<3, S>& pivot, S mass) noexcept {
		if (part_count >= MaxParts) {
			return false;
		}
		if (parent >= 0 && parent >= part_count) {
			return false;
		}
		PartState& p = parts[part_count++];
		p.parent = parent;
		p.pivot = pivot;
		p.mass = mass > T::zero() ? mass : T::zero();
		p.inv_mass = p.mass > T::zero() ? eng::math::div_norm(T::one(), p.mass) : T::zero();
		p.position = pivot;
		p.previous = pivot;
		return true;
	}

	/// Anade una esfera a una parte. `mass` solo se usa para inercia (0 = la parte).
	bool add_sphere(u16 part, const eng::math::Vec<3, S>& center, S radius, S mass) noexcept {
		if (part >= part_count || sphere_count >= MaxSpheres || !(radius > T::zero())) {
			return false;
		}
		if (parts[part].sphere_count == 0u) {
			parts[part].sphere_first = sphere_count;
		}
		local[sphere_count].center = center;
		local[sphere_count].radius = radius;
		const S m = mass > T::zero() ? mass : parts[part].mass;
		const S r2 = eng::math::mul_norm(radius, radius);
		parts[part].inertia.v[0] = parts[part].inertia.v[0] +
					   eng::math::mul_norm(m, eng::math::mul_norm(center.v[1], center.v[1]) +
							       eng::math::mul_norm(center.v[2], center.v[2]) +
							       r2);
		parts[part].inertia.v[1] = parts[part].inertia.v[1] +
					   eng::math::mul_norm(m, eng::math::mul_norm(center.v[0], center.v[0]) +
							       eng::math::mul_norm(center.v[2], center.v[2]) +
							       r2);
		parts[part].inertia.v[2] = parts[part].inertia.v[2] +
					   eng::math::mul_norm(m, eng::math::mul_norm(center.v[0], center.v[0]) +
							       eng::math::mul_norm(center.v[1], center.v[1]) +
							       r2);
		parts[part].sphere_count = parts[part].sphere_count + 1u;
		++sphere_count;
		return true;
	}

	/// Recalcula posiciones iniciales y esferas world.
	void finalize(const eng::math::Vec<3, S>& root = {}) noexcept {
		for (u16 i = 0u; i < part_count; ++i) {
			const s16 parent = parts[i].parent;
			parts[i].position = (parent < 0)
						    ? parts[i].pivot + root
						    : parts[parent].position + parts[i].pivot;
			parts[i].previous = parts[i].position;
		}
		update_world();
	}

	/// Actualiza las esferas world desde las partes.
	void update_world() noexcept {
		for (u16 i = 0u; i < part_count; ++i) {
			const PartState& p = parts[i];
			for (u16 k = 0u; k < p.sphere_count; ++k) {
				const u16 si = p.sphere_first + k;
				world[si].radius = local[si].radius;
				for (int axis = 0; axis < 3; ++axis) {
					world[si].center.v[axis] = p.position.v[axis] + local[si].center.v[axis];
				}
			}
		}
	}

	/// Paso PBD: integracion + joints de bola (3 iteraciones). `ground_y` es el
	/// suelo; las esferas que lo cruzan se corrigen (contacto simple, sin impulso).
	void step(S dt, S gravity, S ground_y, u8 iterations = 3u) noexcept {
		if (part_count == 0u || !(dt > T::zero())) return;
		const S inv_dt = eng::math::div_norm(T::one(), dt);
		for (u16 i = 0u; i < part_count; ++i) {
			PartState& p = parts[i];
			if (p.inv_mass == T::zero()) {
				p.previous = p.position;
				continue;
			}
			eng::math::Vec<3, S> vel {};
			for (int axis = 0; axis < 3; ++axis) {
				vel.v[axis] = eng::math::mul_norm(p.position.v[axis] - p.previous.v[axis], inv_dt);
			}
			vel.v[1] = vel.v[1] + eng::math::mul_norm(gravity, dt);
			p.previous = p.position;
			for (int axis = 0; axis < 3; ++axis) {
				p.position.v[axis] = p.position.v[axis] + eng::math::mul_norm(vel.v[axis], dt);
			}
		}
		for (u8 it = 0u; it < iterations; ++it) {
			for (u16 i = 1u; i < part_count; ++i) {
				const s16 pi = parts[i].parent;
				if (pi < 0) continue;
				PartState& child = parts[i];
				PartState& parent = parts[pi];
				eng::math::Vec<3, S> target {};
				for (int axis = 0; axis < 3; ++axis) {
					target.v[axis] = parent.position.v[axis] + child.pivot.v[axis];
				}
				const S wsum = child.inv_mass + parent.inv_mass;
				if (wsum == T::zero()) continue;
				for (int axis = 0; axis < 3; ++axis) {
					const S delta = target.v[axis] - child.position.v[axis];
					const S corr = eng::math::div_norm(delta, wsum);
					child.position.v[axis] = child.position.v[axis] +
								 eng::math::mul_norm(corr, child.inv_mass);
					parent.position.v[axis] = parent.position.v[axis] -
								  eng::math::mul_norm(corr, parent.inv_mass);
				}
			}
			// Suelo por esfera: corrige la parte que la contiene.
			for (u16 i = 0u; i < part_count; ++i) {
				PartState& p = parts[i];
				if (p.inv_mass == T::zero()) continue;
				for (u16 k = 0u; k < p.sphere_count; ++k) {
					const u16 si = p.sphere_first + k;
					const S floor = ground_y + local[si].radius;
					const S y = p.position.v[1] + local[si].center.v[1];
					if (y < floor) {
						p.position.v[1] = p.position.v[1] + (floor - y);
					}
				}
			}
		}
		update_world();
	}
};

} // namespace eng::sim3d
