#pragma once

/// \file body.hpp
/// **Cuerpo procedural** (`eng::sim`): una cadena de "chunks" (puntos articulados) que
/// resuelve con **FABRIK** (IK iterativa) y se deforma según la conducta y el afecto. Es
/// la capa de representación: no conoce sprites ni huesos concretos, solo puntos; el
/// render dibuja uniendo/rellenando los chunks y el juego elige el estilo (esferas,
/// segmentos, silueta).
///
/// - `ChainBody<S, N>`: cadena genérica sobre el escalar `S` (`double` en host, `q12` en
///   el 68000). `solve(root, target)` mueve el efector final hacia un objetivo manteniendo
///   la raíz fija y las distancias entre chunks (IK). No usa heap.
/// - `BodyPose`: postura en valores con signo (inclinarse, agacharse, estirarse,
///   retroceder, menear la cola, orientar la cabeza).
/// - `pose_from_behavior` / `pose_from_state`: traducen la **conducta** y el **afecto**
///   (`Mind`, vínculo con el objetivo) en postura. Así el cuerpo "expresa" sumisión,
///   cortejo, huida o agresión sin animaciones dibujadas a mano.
///
/// Verificación: HOST-156.

#include <eng/core/geometry.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/behavior.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Postura expresiva, en `[-100, 100]` por eje (0 = neutra).
struct BodyPose {
	eng::s16 lean = 0;    ///< inclinación hacia delante (+) / atrás (-)
	eng::s16 crouch = 0;  ///< agacharse (0..100)
	eng::s16 reach = 0;   ///< estirar hacia delante (0..100)
	eng::s16 recoil = 0;  ///< echarse atrás / encogerse (0..100)
	eng::s16 wag = 0;     ///< meneo de cola (-100..100)
	eng::s16 head_pitch = 0; ///< cabeza arriba (-) / abajo (+) (sumisión)
};

/// Recorta una postura a `[-100, 100]`.
[[nodiscard]] constexpr eng::s16 clamp_pose(eng::s16 v) noexcept {
	return v > 100 ? 100 : (v < -100 ? -100 : v);
}

/// Postura base de una conducta (sin afecto).
[[nodiscard]] constexpr BodyPose pose_from_behavior(Behavior b) noexcept {
	switch (b) {
		case Behavior::Idle: return {0, 10, 0, 0, 0, 0};
		case Behavior::Wander: return {20, 0, 20, 0, 20, 0};
		case Behavior::Hunt: return {40, 10, 60, 0, 0, -10};
		case Behavior::Flee: return {-30, 20, 0, 80, 0, 10};
		case Behavior::SeekFood: return {30, 0, 40, 0, 0, 0};
		case Behavior::Sleep: return {0, 80, 0, 0, 0, 20};
		case Behavior::Socialize: return {0, 0, 20, 0, 40, -10};
		case Behavior::SeekShelter: return {20, 30, 0, 0, 0, 10};
		case Behavior::Tend: return {30, 20, 50, 0, 0, 10};
		case Behavior::Help: return {20, 0, 70, 0, 20, -20};
		case Behavior::Court: return {20, -10, 30, 0, 60, -30};
		case Behavior::Submit: return {0, 70, 0, -40, 0, 40};
		case Behavior::Defy: return {50, -10, 20, 0, 0, -30};
		case Behavior::Teach: return {20, 0, 30, 0, 10, -10};
		case Behavior::Forage: return {30, 10, 40, 0, 0, 0};
		case Behavior::Avenge: return {60, 0, 80, 0, 0, -30};
		default: return {};
	}
}

/// Postura derivada de la conducta **y del afecto**. `bond` es el vínculo con el objetivo
/// (`bond_score`): negativo añade agresividad, positivo añade apertura.
[[nodiscard]] constexpr BodyPose pose_from_state(Behavior b, const Mind& mind,
						 eng::s16 bond = 0) noexcept {
	BodyPose p = pose_from_behavior(b);
	// Miedo: agacharse y retroceder. Ira: inclinarse y estirarse. Amor/cordialidad: cola.
	const eng::s16 fear = static_cast<eng::s16>(mind.emotions.fear) - 128;
	const eng::s16 anger = static_cast<eng::s16>(mind.emotions.anger) - 128;
	const eng::s16 joy = static_cast<eng::s16>(mind.emotions.joy) - 128;
	const eng::s16 compassion = static_cast<eng::s16>(mind.emotions.compassion) - 128;
	p.crouch = clamp_pose(p.crouch + fear / 3);
	p.recoil = clamp_pose(p.recoil + fear / 2);
	p.lean = clamp_pose(p.lean + anger / 3);
	p.reach = clamp_pose(p.reach + (anger + compassion) / 4);
	p.wag = clamp_pose(p.wag + joy / 2);
	p.head_pitch = clamp_pose(p.head_pitch + (fear - joy) / 4);
	// Vínculo: un enemigo cercano tensa la postura; un aliado la relaja.
	if (bond < 0) {
		p.lean = clamp_pose(p.lean - bond / 3);
		p.head_pitch = clamp_pose(p.head_pitch + bond / 4); // cabeza baja/adelante (tension)
	} else if (bond > 0) {
		p.wag = clamp_pose(p.wag + bond / 3);
	}
	return p;
}

/// Cadena articulada genérica sobre el escalar `S`.
template <class S, eng::u8 N>
struct ChainBody {
	static_assert(N >= 2u, "ChainBody: se necesitan al menos 2 chunks");
	using Vec2 = eng::math::Vec<2, S>;

	Vec2 joints[N] {};
	S segment {};

	/// Endereza la cadena desde `origin` hacia +X con la longitud de segmento `seg`.
	constexpr void reset(const Vec2& origin, S seg) noexcept {
		segment = seg;
		const S zero = eng::math::scalar_traits<S>::zero();
		const Vec2 step {seg, zero};
		for (eng::u8 i = 0; i < N; ++i) {
			joints[i] = origin + eng::math::vscale(
						    step, eng::math::scalar_traits<S>::from_int(
							     static_cast<int>(i)));
		}
	}

	[[nodiscard]] constexpr const Vec2& head() const noexcept { return joints[0]; }
	[[nodiscard]] constexpr const Vec2& tail() const noexcept { return joints[N - 1u]; }
	[[nodiscard]] constexpr const Vec2& joint(eng::u8 i) const noexcept { return joints[i]; }

	/// Resuelve IK (FABRIK): coloca la punta en `target` con la raíz en `root`,
	/// conservando la longitud de los segmentos. `iters` pasos de relajación.
	constexpr void solve(const Vec2& root, const Vec2& target, eng::u8 iters = 8u) noexcept {
		const S span = eng::math::scalar_traits<S>::one();
		const S max_reach = eng::math::mul_norm(span, eng::math::scalar_traits<S>::from_int(
									  static_cast<int>(N - 1u)));
		const S max_len = eng::math::mul_norm(segment, max_reach);
		const Vec2 to_target = target - root;

		// Fuera de alcance: cadena recta hacia el objetivo.
		if (!(eng::math::length(to_target) < max_len)) {
			const Vec2 dir = eng::math::normalize(to_target);
			for (eng::u8 i = 0; i < N; ++i) {
				joints[i] = root + eng::math::vscale(
							    dir, eng::math::mul_norm(
									 segment, eng::math::scalar_traits<S>::from_int(
											  static_cast<int>(i))));
			}
			return;
		}

		for (eng::u8 it = 0; it < iters; ++it) {
			// Pasada hacia atrás: la punta va al objetivo.
			joints[N - 1u] = target;
			for (eng::u8 k = static_cast<eng::u8>(N - 1u); k > 0u; --k) {
				const Vec2 dir = eng::math::normalize(joints[k - 1u] - joints[k]);
				joints[k - 1u] = joints[k] + eng::math::vscale(dir, segment);
			}
			// Pasada hacia delante: la raíz queda fija.
			joints[0] = root;
			for (eng::u8 k = 1u; k < N; ++k) {
				const Vec2 dir = eng::math::normalize(joints[k] - joints[k - 1u]);
				joints[k] = joints[k - 1u] + eng::math::vscale(dir, segment);
			}
			if (eng::math::length(target - joints[N - 1u]) <
			    eng::math::div_norm(eng::math::scalar_traits<S>::from_int(1),
						eng::math::scalar_traits<S>::from_int(100))) {
				break;
			}
		}
	}

	/// Aplica una postura a la cadena ya resuelta. `scale` es la amplitud en unidades del
	/// escalar (p. ej. 1 en `double` o el segmento en `q12`).
	constexpr void apply_pose(const BodyPose& pose, S scale) noexcept {
		const S cent = eng::math::scalar_traits<S>::from_int(100);
		const Vec2 axis = eng::math::normalize(tail() - head());
		const Vec2 normal {-axis.v[1], axis.v[0]};
		const S crouch = eng::math::mul_norm(
			scale, eng::math::div_norm(eng::math::scalar_traits<S>::from_int(pose.crouch), cent));
		const S lean = eng::math::mul_norm(
			scale, eng::math::div_norm(eng::math::scalar_traits<S>::from_int(pose.lean), cent));
		const S recoil = eng::math::mul_norm(
			scale, eng::math::div_norm(eng::math::scalar_traits<S>::from_int(pose.recoil), cent));
		const S wag = eng::math::mul_norm(
			scale, eng::math::div_norm(eng::math::scalar_traits<S>::from_int(pose.wag), cent));
		for (eng::u8 i = 0; i < N; ++i) {
			const S fi = eng::math::scalar_traits<S>::from_int(static_cast<int>(i));
			const S t = eng::math::div_norm(
				fi, eng::math::scalar_traits<S>::from_int(static_cast<int>(N - 1u)));
			// Agacharse (Y hacia abajo) y retroceder (a lo largo de -eje).
			joints[i].v[1] = joints[i].v[1] - crouch;
			joints[i] = joints[i] - eng::math::vscale(axis, eng::math::mul_norm(recoil, t));
			// Curvar/inclinar hacia delante con un desplazamiento creciente.
			joints[i] = joints[i] + eng::math::vscale(normal, eng::math::mul_norm(lean, t));
			// Meneo de cola: solo la mitad trasera, alternando lado.
			if (static_cast<eng::u8>(i * 2u) >= N) {
				const S side = (i % 2u == 0u)
						       ? eng::math::scalar_traits<S>::one()
						       : eng::math::scalar_traits<S>::from_int(-1);
				joints[i].v[1] = joints[i].v[1] + eng::math::mul_norm(wag, side);
			}
		}
	}
};

} // namespace eng::sim
