#pragma once

/// \file model_body3d.hpp
/// **Puente asset -> fisica** (G7): convierte las partes/esferas de un
/// `Model3DAssetView` v2 en un `CompoundBody3D`. Las masas se toman de
/// `ModelPart::mass`; si es 0, se aproximan con `density * radius^2` (modelo de
/// juego, no volumen exacto). La jerarquia de partes se aplica como traslacion de
/// pivotes: es la base para un personaje articulado sin solver de joints.

#include <eng/assets/model3d.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/sim/articulated_body3d.hpp>
#include <eng/sim/compound_body3d.hpp>

namespace eng::sim3d {

/// Construye un cuerpo compuesto a partir de un Model3D v2.
/// \tparam S escalar de fisica.
/// \tparam MaxSpheres capacidad del cuerpo.
/// \param view asset v2 validado.
/// \param body salida; se limpia antes.
/// \return true si el asset tiene partes/esferas validas y caben.
template <class S, eng::u16 MaxSpheres>
[[nodiscard]] bool build_compound_from_model(const eng::assets::Model3DAssetView& view,
					     CompoundBody3D<S, MaxSpheres>& body) noexcept {
	using T = eng::math::scalar_traits<S>;
	body.clear();
	const eng::u16 np = view.part_count();
	if (np == 0u) {
		return false;
	}
	for (eng::u16 pi = 0u; pi < np; ++pi) {
		const eng::assets::ModelPart part = view.part(pi);
		if (part.sphere_count == 0u) {
			continue;
		}
		const S part_mass = T::from_int(part.mass);
		const S density_q8 = part.density != 0u ? T::from_int(part.density) : T::from_int(256);
		for (eng::u16 k = 0u; k < part.sphere_count; ++k) {
			const eng::assets::ModelSphere s = view.sphere(part.sphere_first + k);
			eng::math::Vec<3, S> c {};
			for (int axis = 0; axis < 3; ++axis) {
				c.v[axis] = T::from_int(static_cast<int>(part.pivot[axis]) +
							static_cast<int>(s.center[axis]));
			}
			const S radius = T::from_int(s.radius);
			S m = T::zero();
			if (part_mass > T::zero()) {
				m = eng::math::div_norm(part_mass, T::from_int(part.sphere_count));
			} else {
				// Aproximacion de juego: densidad Q8.8 * r^2.
				const S density = eng::math::div_norm(density_q8, T::from_int(256));
				m = eng::math::mul_norm(density, eng::math::mul_norm(radius, radius));
			}
			if (!body.add_sphere(c, radius, m)) {
				body.clear();
				return false;
			}
		}
	}
	body.finalize();
	return body.count != 0u;
}

/// Construye un cuerpo articulado PBD desde las partes/esferas del asset v2.
/// La rotacion local del asset queda como metadato del rig; el solver actual
/// resuelve pivotes/joints de bola por traslacion (primer corte de G7).
/// \tparam S escalar de fisica.
/// \tparam MaxParts capacidad de partes.
/// \tparam MaxSpheres capacidad de esferas.
/// \param view asset v2 validado.
/// \param body salida.
/// \return true si cabe y se construyo.
template <class S, eng::u16 MaxParts, eng::u16 MaxSpheres>
[[nodiscard]] bool build_articulated_from_model(const eng::assets::Model3DAssetView& view,
						ArticulatedBody3D<S, MaxParts, MaxSpheres>& body) noexcept {
	using T = eng::math::scalar_traits<S>;
	body.clear();
	const eng::u16 np = view.part_count();
	if (np == 0u || np > MaxParts) {
		return false;
	}
	for (eng::u16 pi = 0u; pi < np; ++pi) {
		const eng::assets::ModelPart part = view.part(pi);
		eng::math::Vec<3, S> pivot {};
		for (int axis = 0; axis < 3; ++axis) {
			pivot.v[axis] = T::from_int(part.pivot[axis]);
		}
		const S mass = T::from_int(part.mass);
		if (!body.add_part(part.parent, pivot, mass)) {
			body.clear();
			return false;
		}
	}
	for (eng::u16 pi = 0u; pi < np; ++pi) {
		const eng::assets::ModelPart part = view.part(pi);
		for (eng::u16 k = 0u; k < part.sphere_count; ++k) {
			const eng::assets::ModelSphere s = view.sphere(part.sphere_first + k);
			eng::math::Vec<3, S> center {};
			for (int axis = 0; axis < 3; ++axis) {
				center.v[axis] = T::from_int(s.center[axis]);
			}
			const S radius = T::from_int(s.radius);
			const S m = part.mass > 0u
					    ? eng::math::div_norm(T::from_int(part.mass),
								  T::from_int(part.sphere_count))
					    : T::from_int(1);
			if (!body.add_sphere(pi, center, radius, m)) {
				body.clear();
				return false;
			}
		}
	}
	body.finalize();
	return body.part_count != 0u;
}
} // namespace eng::sim3d
