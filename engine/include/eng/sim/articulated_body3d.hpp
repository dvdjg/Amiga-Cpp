#pragma once

/// \file articulated_body3d.hpp
/// **Cuerpo articulado por position-based dynamics (PBD) con estado angular**:
/// partes con padre, pivote local, masa, inercia diagonal, esferas y orientacion
/// local en turns. El paso integra traslacion y rotacion, resuelve joints de bola
/// por correccion de posiciones y, para partes marcadas como bisagra, limita el
/// giro a un eje. La inercia se calcula de verdad a partir de las esferas
/// (termino propio + paralelo) y los impulsos descentrados actualizan la
/// velocidad angular.
///
/// Es un solver de personajes/props para A500, no un motor de cuerpos rigidos
/// general: la orientacion se representa con tres angulos Euler locales (barato
/// y compatible con la tabla de seno Q4.12), no con quaternions.

#include <eng/core/math/geometry.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/collision3d.hpp>

namespace eng::sim3d {

/// Cuerpo articulado de capacidad fija.
/// \tparam S escalar de posicion/fuerza (q0 en 68000).
/// \tparam MaxParts numero maximo de partes (>0).
/// \tparam MaxSpheres numero maximo de esferas (>0).
template <class S, eng::u16 MaxParts, eng::u16 MaxSpheres>
struct ArticulatedBody3D {
	static_assert(MaxParts > 0u && MaxSpheres > 0u, "ArticulatedBody3D capacidades > 0");
	using T = eng::math::scalar_traits<S>;
	using R = eng::math::scalar_traits<eng::real>;
	using VecR = eng::math::Vec<3, eng::real>;
	using MatR = eng::math::Mat<3, eng::real>;

	enum class Joint : eng::u8 {
		ball = 0u, ///< rotacion libre respecto al padre.
		hinge = 1u, ///< un solo eje local, con limites.
		slide = 2u, ///< desplazamiento lineal sobre un eje local, con limites.
	};

	struct PartState {
		s16 parent = -1; ///< indice de padre; -1 = raiz.
		eng::math::Vec<3, S> pivot {}; ///< pivote local respecto al padre.
		eng::math::Vec<3, S> position {}; ///< posicion world del pivote.
		eng::math::Vec<3, S> previous {}; ///< posicion previa (PBD).
		eng::math::Vec<3, S> velocity {}; ///< velocidad lineal world.
		S inv_mass = T::zero(); ///< 1/masa; 0 = estatico.
		S mass = T::zero(); ///< masa de la parte.
		VecR inertia {}; ///< inercia diagonal local (unidades de juego).
		VecR inv_inertia {}; ///< 1/inercia por eje.
		VecR angles {}; ///< orientacion local en turns (fraccion 0..1).
		VecR angular_velocity {}; ///< velocidad angular local (turns/s).
		MatR local_orientation = MatR::identity(); ///< matriz de `angles`.
		MatR world_orientation = MatR::identity(); ///< orientacion world.
		Joint joint = Joint::ball; ///< tipo de joint con el padre.
		u8 hinge_axis = 0u; ///< eje local de la bisagra (0=x,1=y,2=z).
		eng::real hinge_min = R::from_int(-1); ///< limite inferior (turns).
		eng::real hinge_max = R::from_int(1); ///< limite superior (turns).
		S slide_min = T::zero(); ///< limite lineal inferior (unidades de S).
		S slide_max = T::zero(); ///< limite lineal superior (unidades de S).
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
		p.velocity = {};
		p.angles = {};
		p.angular_velocity = {};
		p.local_orientation = MatR::identity();
		p.world_orientation = MatR::identity();
		p.joint = Joint::ball;
		p.inertia = {};
		p.inv_inertia = {};
		p.sphere_first = 0u;
		p.sphere_count = 0u;
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
		const eng::real mr = to_real(m);
		const eng::real x = to_real(center.v[0]);
		const eng::real y = to_real(center.v[1]);
		const eng::real z = to_real(center.v[2]);
		const eng::real r = to_real(radius);
		const eng::real r2 = eng::math::mul_norm(r, r);
		const eng::real self = eng::math::mul_norm(
			eng::math::div_norm(R::from_int(2), R::from_int(5)),
			eng::math::mul_norm(mr, r2));
		parts[part].inertia.v[0] = parts[part].inertia.v[0] +
			eng::math::mul_norm(mr, eng::math::mul_norm(y, y) + eng::math::mul_norm(z, z)) + self;
		parts[part].inertia.v[1] = parts[part].inertia.v[1] +
			eng::math::mul_norm(mr, eng::math::mul_norm(x, x) + eng::math::mul_norm(z, z)) + self;
		parts[part].inertia.v[2] = parts[part].inertia.v[2] +
			eng::math::mul_norm(mr, eng::math::mul_norm(x, x) + eng::math::mul_norm(y, y)) + self;
		for (u8 k = 0u; k < 3u; ++k) {
			parts[part].inv_inertia.v[k] =
				parts[part].inertia.v[k] > R::zero()
					? eng::math::div_norm(R::one(), parts[part].inertia.v[k])
					: R::zero();
		}
		parts[part].sphere_count = parts[part].sphere_count + 1u;
		++sphere_count;
		return true;
	}

	/// Configura una bisagra local con limites en turns. El eje es 0=x, 1=y, 2=z.
	bool set_hinge(u16 part, u8 axis, eng::real min_turns, eng::real max_turns) noexcept {
		if (part >= part_count || axis >= 3u || !(min_turns <= max_turns)) {
			return false;
		}
		PartState& p = parts[part];
		p.joint = Joint::hinge;
		p.hinge_axis = axis;
		p.hinge_min = min_turns;
		p.hinge_max = max_turns;
		clamp_hinge(p);
		return true;
	}

	/// Configura un joint telescopico (desplazamiento lineal) sobre un eje local.
	bool set_slide(u16 part, u8 axis, S min_distance, S max_distance) noexcept {
		if (part >= part_count || axis >= 3u || min_distance > max_distance) return false;
		PartState& p = parts[part];
		p.joint = Joint::slide;
		p.hinge_axis = axis;
		p.slide_min = min_distance;
		p.slide_max = max_distance;
		return true;
	}
/// Fija la orientacion local (turns) y refresca las matrices.
	bool set_angles(u16 part, const VecR& angles) noexcept {
		if (part >= part_count) {
			return false;
		}
		parts[part].angles = angles;
		refresh_orientation(parts[part]);
		update_world();
		return true;
	}

	/// Recalcula posiciones iniciales, matrices y esferas world.
	void finalize(const eng::math::Vec<3, S>& root = {}) noexcept {
		for (u16 i = 0u; i < part_count; ++i) {
			PartState& p = parts[i];
			refresh_orientation(p);
			const MatR parent_rot = p.parent >= 0 ? parts[p.parent].world_orientation
							      : MatR::identity();
			p.world_orientation = parent_rot * p.local_orientation;
			p.position = (p.parent < 0)
					     ? p.pivot + root
					     : parts[p.parent].position + parent_rot * p.pivot;
			p.previous = p.position;
			p.velocity = {};
		}
		update_world();
	}

	/// Aplica un impulso en un punto world (fuerza lineal + par descentrado).
	void apply_impulse(u16 part, const eng::math::Vec<3, S>& impulse,
			   const eng::math::Vec<3, S>& point) noexcept {
		if (part >= part_count) {
			return;
		}
		PartState& p = parts[part];
		if (p.inv_mass == T::zero()) {
			return;
		}
		for (u8 k = 0u; k < 3u; ++k) {
			p.velocity.v[k] = p.velocity.v[k] + eng::math::mul_norm(impulse.v[k], p.inv_mass);
		}
		eng::math::Vec<3, S> arm {};
		for (u8 k = 0u; k < 3u; ++k) {
			arm.v[k] = point.v[k] - p.position.v[k];
		}
		const eng::math::Vec<3, S> torque = cross3(arm, impulse);
		for (u8 k = 0u; k < 3u; ++k) {
			if (p.joint == Joint::hinge && k != p.hinge_axis) continue;
			p.angular_velocity.v[k] = p.angular_velocity.v[k] +
				eng::math::mul_norm(to_real(torque.v[k]), p.inv_inertia.v[k]);
		}
	}

	/// Fija la velocidad angular local (turns/s); respeta el eje de la bisagra.
	void set_angular_velocity(u16 part, const VecR& velocity) noexcept {
		if (part >= part_count) return;
		PartState& p = parts[part];
		for (u8 k = 0u; k < 3u; ++k) {
			if (p.joint == Joint::hinge && k != p.hinge_axis) continue;
			p.angular_velocity.v[k] = velocity.v[k];
		}
	}

	/// Aplica un par world directamente a la velocidad angular.
	void apply_torque(u16 part, const VecR& torque) noexcept {
		if (part >= part_count) {
			return;
		}
		PartState& p = parts[part];
		for (u8 k = 0u; k < 3u; ++k) {
			if (p.joint == Joint::hinge && k != p.hinge_axis) continue;
			p.angular_velocity.v[k] =
				p.angular_velocity.v[k] +
				eng::math::mul_norm(torque.v[k], p.inv_inertia.v[k]);
		}
	}

	/// Actualiza matrices locales/world y esferas world. Los padres van antes.
	void update_world() noexcept {
		for (u16 i = 0u; i < part_count; ++i) {
			PartState& p = parts[i];
			const MatR parent_rot = p.parent >= 0 ? parts[p.parent].world_orientation
							      : MatR::identity();
			p.world_orientation = parent_rot * p.local_orientation;
			for (u16 k = 0u; k < p.sphere_count; ++k) {
				const u16 si = p.sphere_first + k;
				const eng::math::Vec<3, S> rotated = p.world_orientation * local[si].center;
				world[si].radius = local[si].radius;
				for (u8 axis = 0u; axis < 3u; ++axis) {
					world[si].center.v[axis] = p.position.v[axis] + rotated.v[axis];
				}
			}
		}
	}

	/// Paso PBD: integracion lineal/angular + joints de bola y bisagra + suelo.
	/// `ground_y` es el suelo; las esferas que lo cruzan se corrigen.
	void step(S dt, S gravity, S ground_y, u8 iterations = 3u) noexcept {
		if (part_count == 0u || !(dt > T::zero())) return;
		const eng::real dt_r = to_real(dt);
		for (u16 i = 0u; i < part_count; ++i) {
			PartState& p = parts[i];
			if (p.inv_mass == T::zero()) {
				p.previous = p.position;
				p.velocity = {};
				p.angular_velocity = {};
				continue;
			}
			p.velocity.v[1] = p.velocity.v[1] + eng::math::mul_norm(gravity, dt);
			p.previous = p.position;
			for (u8 k = 0u; k < 3u; ++k) {
				p.position.v[k] = p.position.v[k] + eng::math::mul_norm(p.velocity.v[k], dt);
			}
			for (u8 k = 0u; k < 3u; ++k) {
				p.angles.v[k] = p.angles.v[k] +
					eng::math::mul_norm(p.angular_velocity.v[k], dt_r);
			}
			wrap_turns(p.angles);
			clamp_hinge(p);
			refresh_orientation(p);
		}
		update_world();
		for (u8 it = 0u; it < iterations; ++it) {
			for (u16 i = 1u; i < part_count; ++i) {
				const s16 pi = parts[i].parent;
				if (pi < 0) continue;
				PartState& child = parts[i];
				PartState& parent = parts[pi];
				eng::math::Vec<3, S> target = parent.position + parent.world_orientation * child.pivot;
				if (child.joint == Joint::slide) {
					eng::math::Vec<3, S> axis {};
					axis.v[child.hinge_axis] = T::one();
					const eng::math::Vec<3, S> aw = parent.world_orientation * axis;
					S d = T::zero();
					for (u8 k = 0u; k < 3u; ++k) {
						d = d + eng::math::mul_norm(child.position.v[k] - parent.position.v[k], aw.v[k]);
					}
					if (d < child.slide_min) d = child.slide_min;
					if (d > child.slide_max) d = child.slide_max;
					for (u8 k = 0u; k < 3u; ++k) {
						target.v[k] = parent.position.v[k] + eng::math::mul_norm(aw.v[k], d);
					}
				}
				const S wsum = child.inv_mass + parent.inv_mass;
				if (wsum == T::zero()) continue;
				for (u8 axis = 0u; axis < 3u; ++axis) {
					const S delta = target.v[axis] - child.position.v[axis];
					const S corr = eng::math::div_norm(delta, wsum);
					child.position.v[axis] = child.position.v[axis] +
								 eng::math::mul_norm(corr, child.inv_mass);
					parent.position.v[axis] = parent.position.v[axis] -
								  eng::math::mul_norm(corr, parent.inv_mass);
				}
			}
			for (u16 i = 0u; i < part_count; ++i) {
				PartState& p = parts[i];
				if (p.inv_mass == T::zero()) continue;
				for (u16 k = 0u; k < p.sphere_count; ++k) {
					const u16 si = p.sphere_first + k;
					const eng::math::Vec<3, S> rc = p.world_orientation * local[si].center;
					const S floor = ground_y + local[si].radius;
					const S y = p.position.v[1] + rc.v[1];
					if (y < floor) {
						p.position.v[1] = p.position.v[1] + (floor - y);
					}
				}
			}
		}
		for (u16 i = 0u; i < part_count; ++i) {
			PartState& p = parts[i];
			if (p.inv_mass == T::zero()) continue;
			for (u8 k = 0u; k < 3u; ++k) {
				p.velocity.v[k] = eng::math::div_norm(p.position.v[k] - p.previous.v[k], dt);
			}
			const eng::real damp = R::one() - eng::math::mul_norm(eng::math::div_norm(R::one(), R::from_int(50)), dt_r);
			p.angular_velocity = p.angular_velocity * damp;
		}
		update_world();
	}

private:
	/// Convierte un escalar de simulacion a `eng::real` sin pasar por float.
	static eng::real to_real(S v) noexcept {
		if constexpr (T::needs_normalize) {
			return R::from_int(T::to_int(v));
		} else {
			return eng::real(v);
		}
	}

	/// Producto cruz 3D (el algebra generica solo trae el 2D).
	static eng::math::Vec<3, S> cross3(const eng::math::Vec<3, S>& a,
					  const eng::math::Vec<3, S>& b) noexcept {
		return {{eng::math::mul_norm(a.v[1], b.v[2]) - eng::math::mul_norm(a.v[2], b.v[1]),
			 eng::math::mul_norm(a.v[2], b.v[0]) - eng::math::mul_norm(a.v[0], b.v[2]),
			 eng::math::mul_norm(a.v[0], b.v[1]) - eng::math::mul_norm(a.v[1], b.v[0])}};
	}

	/// `M = Rx * Ry * Rz` desde tres angulos en turns.
	static void euler_turns(MatR& m, const VecR& turns) noexcept {
		eng::real sx = R::zero();
		eng::real cx = R::zero();
		eng::real sy = R::zero();
		eng::real cy = R::zero();
		eng::real sz = R::zero();
		eng::real cz = R::zero();
		eng::math::sincos(eng::math::Angle<eng::real, eng::math::angle::turns> {turns.v[0]}, sx, cx);
		eng::math::sincos(eng::math::Angle<eng::real, eng::math::angle::turns> {turns.v[1]}, sy, cy);
		eng::math::sincos(eng::math::Angle<eng::real, eng::math::angle::turns> {turns.v[2]}, sz, cz);
		const eng::real tmp0 = eng::math::mul_norm(sy, cz);
		const eng::real tmp1 = eng::math::mul_norm(sy, sz);
		m.m[0][0] = eng::math::mul_norm(cy, cz);
		m.m[0][1] = -eng::math::mul_norm(cy, sz);
		m.m[0][2] = sy;
		m.m[1][0] = eng::math::mul_norm(cx, sz) + eng::math::mul_norm(sx, tmp0);
		m.m[1][1] = eng::math::mul_norm(cx, cz) - eng::math::mul_norm(sx, tmp1);
		m.m[1][2] = -eng::math::mul_norm(sx, cy);
		m.m[2][0] = eng::math::mul_norm(sx, sz) - eng::math::mul_norm(cx, tmp0);
		m.m[2][1] = eng::math::mul_norm(sx, cz) + eng::math::mul_norm(cx, tmp1);
		m.m[2][2] = eng::math::mul_norm(cx, cy);
	}

	static void refresh_orientation(PartState& p) noexcept { euler_turns(p.local_orientation, p.angles); }

	/// Lleva los tres angulos al rango [0,1) de turns.
	static void wrap_turns(VecR& angles) noexcept {
		for (u8 k = 0u; k < 3u; ++k) {
			while (angles.v[k] >= R::one()) angles.v[k] = angles.v[k] - R::one();
			while (angles.v[k] < R::zero()) angles.v[k] = angles.v[k] + R::one();
		}
	}

	/// Anula los ejes libres de la bisagra y limita el eje permitido.
	static void clamp_hinge(PartState& p) noexcept {
		if (p.joint != Joint::hinge || p.hinge_axis >= 3u) return;
		for (u8 k = 0u; k < 3u; ++k) {
			if (k != p.hinge_axis) {
				p.angles.v[k] = R::zero();
				p.angular_velocity.v[k] = R::zero();
			}
		}
		if (p.angles.v[p.hinge_axis] < p.hinge_min) {
			p.angles.v[p.hinge_axis] = p.hinge_min;
			if (p.angular_velocity.v[p.hinge_axis] < R::zero()) {
				p.angular_velocity.v[p.hinge_axis] = R::zero();
			}
		} else if (p.angles.v[p.hinge_axis] > p.hinge_max) {
			p.angles.v[p.hinge_axis] = p.hinge_max;
			if (p.angular_velocity.v[p.hinge_axis] > R::zero()) {
				p.angular_velocity.v[p.hinge_axis] = R::zero();
			}
		}
	}
};

} // namespace eng::sim3d
