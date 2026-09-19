#pragma once

/// \file steering.hpp
/// **Steering behaviors** (`eng::ai::steering`): velocidades de movimiento continuo
/// para agentes, genéricas sobre el escalar (`float`, `q12`…). Sobre el vocabulario
/// geométrico de `eng::math` (`Vec<2,S>`, `length`, `normalize`, `vscale`), sin `float`
/// obligatorio: con `q12` (y `fixed_math.hpp` incluido) usan tablas/`divs.w` nativos.
///
/// - `seek`/`flee`: ir hacia / alejarse de un punto a `max_speed`.
/// - `arrive`: como `seek` pero **frena** dentro de `slow_radius`.
/// - `separation`/`cohesion`/`alignment`: los tres términos del **flocking**; `flock` los
///   combina con pesos (`FlockWeights`).
///
/// Los vectores devueltos son **velocidades deseadas**; integrarlas y limitar el giro es
/// del juego. `normalize` de un vector nulo devuelve cero (sin NaN ni división por cero).
///
/// Límite con `q12`: `length_sq` es la suma de dos cuadrados y desborda el rango del
/// fixed (≈ ±8) si las componentes pasan de ~2; para vectores mayores, reescalar antes.
///
/// Uso:
///   using V = eng::math::Vec<2, q12>;
///   const V v = eng::ai::seek(pos, target, q12{256});
///   const V a = eng::ai::arrive(pos, target, q12{256}, q12{1024});
///
/// Verificación: HOST-115.

#include <eng/core/geometry.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::ai {

template <class S>
using SteerVec = eng::math::Vec<2, S>;

/// Pesos del flocking (en el mismo espacio que la velocidad).
template <class S>
struct FlockWeights {
	S separation {};
	S cohesion {};
	S alignment {};
};

/// Velocidad deseada hacia `target` de magnitud `max_speed`.
template <class S>
[[nodiscard]] constexpr SteerVec<S> seek(const SteerVec<S>& pos, const SteerVec<S>& target,
					 S max_speed) {
	return eng::math::vscale(eng::math::normalize(target - pos), max_speed);
}

/// Velocidad deseada para alejarse de `threat` de magnitud `max_speed`.
template <class S>
[[nodiscard]] constexpr SteerVec<S> flee(const SteerVec<S>& pos, const SteerVec<S>& threat,
					 S max_speed) {
	return eng::math::vscale(eng::math::normalize(pos - threat), max_speed);
}

/// Velocidad hacia `target` que decrece linealmente por dentro de `slow_radius` (frena
/// al llegar). A `slow_radius` o más, es `seek`; a distancia cero, cero.
template <class S>
[[nodiscard]] constexpr SteerVec<S> arrive(const SteerVec<S>& pos, const SteerVec<S>& target,
					   S max_speed, S slow_radius) {
	const SteerVec<S> delta = target - pos;
	const S dist = eng::math::length(delta);
	if (dist == eng::math::scalar_traits<S>::zero()) {
		return SteerVec<S> {};
	}
	S speed = max_speed;
	if (dist < slow_radius) {
		speed = eng::math::mul_norm(max_speed,
					    eng::math::div_norm(dist, slow_radius));
	}
	return eng::math::vscale(eng::math::normalize(delta), speed);
}

/// Separación: alejarse de los vecinos dentro de `radius` (evita aglomeraciones).
/// Devuelve cero si no hay vecinos demasiado cerca.
template <class S>
[[nodiscard]] constexpr SteerVec<S> separation(const SteerVec<S>& self,
					       eng::Span<const SteerVec<S>> neighbors, S radius,
					       S max_speed) {
	SteerVec<S> push {};
	for (eng::usize i = 0; i < neighbors.size(); ++i) {
		const SteerVec<S> back = self - neighbors[i];
		const S dist = eng::math::length(back);
		if (dist > eng::math::scalar_traits<S>::zero() && dist < radius) {
			push = push + back;
		}
	}
	return eng::math::vscale(eng::math::normalize(push), max_speed);
}

/// Cohesión: acercarse al centroide de los vecinos.
template <class S>
[[nodiscard]] constexpr SteerVec<S> cohesion(const SteerVec<S>& self,
					     eng::Span<const SteerVec<S>> neighbors, S max_speed) {
	if (neighbors.empty()) {
		return SteerVec<S> {};
	}
	SteerVec<S> sum {};
	for (eng::usize i = 0; i < neighbors.size(); ++i) {
		sum = sum + neighbors[i];
	}
	const S inv = eng::math::div_norm(
		eng::math::scalar_traits<S>::one(),
		eng::math::scalar_traits<S>::from_int(static_cast<int>(neighbors.size())));
	const SteerVec<S> centroid = eng::math::vscale(sum, inv);
	return eng::math::vscale(eng::math::normalize(centroid - self), max_speed);
}

/// Alineación: imitar la velocidad media de los vecinos.
template <class S>
[[nodiscard]] constexpr SteerVec<S> alignment(const SteerVec<S>& self_velocity,
					      eng::Span<const SteerVec<S>> neighbor_velocities,
					      S max_speed) {
	if (neighbor_velocities.empty()) {
		return SteerVec<S> {};
	}
	SteerVec<S> sum {};
	for (eng::usize i = 0; i < neighbor_velocities.size(); ++i) {
		sum = sum + neighbor_velocities[i];
	}
	const S inv = eng::math::div_norm(
		eng::math::scalar_traits<S>::one(),
		eng::math::scalar_traits<S>::from_int(static_cast<int>(neighbor_velocities.size())));
	const SteerVec<S> avg = eng::math::vscale(sum, inv);
	return eng::math::vscale(eng::math::normalize(avg - self_velocity), max_speed);
}

/// Flocking: combinación ponderada de separación, cohesión y alineación. Cada término se
/// normaliza (magnitud 1) y se escala por su peso en `FlockWeights`.
template <class S>
[[nodiscard]] constexpr SteerVec<S> flock(const SteerVec<S>& self,
					  const SteerVec<S>& self_velocity,
					  eng::Span<const SteerVec<S>> neighbor_positions,
					  eng::Span<const SteerVec<S>> neighbor_velocities,
					  S radius, const FlockWeights<S>& weights) {
	const S one = eng::math::scalar_traits<S>::one();
	SteerVec<S> total = eng::math::vscale(
		separation(self, neighbor_positions, radius, one), weights.separation);
	total = total + eng::math::vscale(cohesion(self, neighbor_positions, one),
					  weights.cohesion);
	total = total + eng::math::vscale(alignment(self_velocity, neighbor_velocities, one),
					  weights.alignment);
	return total;
}

/// Persecución: como `seek` a la **posición prevista** del objetivo dentro de
/// `distancia/max_speed` ticks (el objetivo se mueve con `target_velocity`).
template <class S>
[[nodiscard]] constexpr SteerVec<S> pursue(const SteerVec<S>& pos, const SteerVec<S>& target,
					   const SteerVec<S>& target_velocity, S max_speed) {
	if (max_speed == eng::math::scalar_traits<S>::zero()) {
		return SteerVec<S> {};
	}
	const S t = eng::math::div_norm(eng::math::distance(pos, target), max_speed);
	const SteerVec<S> predicted = target + eng::math::vscale(target_velocity, t);
	return seek(pos, predicted, max_speed);
}

/// Evasión de un objetivo móvil: `flee` de su posición prevista.
template <class S>
[[nodiscard]] constexpr SteerVec<S> evade(const SteerVec<S>& pos, const SteerVec<S>& target,
					  const SteerVec<S>& target_velocity, S max_speed) {
	if (max_speed == eng::math::scalar_traits<S>::zero()) {
		return SteerVec<S> {};
	}
	const S t = eng::math::div_norm(eng::math::distance(pos, target), max_speed);
	const SteerVec<S> predicted = target + eng::math::vscale(target_velocity, t);
	return flee(pos, predicted, max_speed);
}

/// Deambular clásico: un objetivo que orbita sobre un círculo de radio `wander_radius`
/// por delante del agente. El `heading` (ángulo) y el `jitter` (desviación angular) los
/// aporta el juego (deterministas); `heading + jitter` mueve el punto del círculo.
template <class S>
[[nodiscard]] constexpr SteerVec<S> wander(const SteerVec<S>& pos, S heading, S wander_radius,
					   S jitter, S max_speed) {
	using Rad = eng::math::Angle<S, eng::math::angle::radians>;
	const SteerVec<S> ahead = pos + eng::math::vscale(eng::math::from_angle(Rad {heading}), wander_radius);
	const SteerVec<S> target =
		ahead + eng::math::vscale(eng::math::from_angle(Rad {heading + jitter}), wander_radius);
	return seek(pos, target, max_speed);
}

/// Obstáculo circular (centro + radio) para `avoid_circles`.
template <class S>
struct SteerCircle {
	SteerVec<S> center {};
	S radius {};
};

/// Evasión de obstáculos: suma al vector `desired` un empuje **lateral** (perpendicular
/// a `desired`) por cada obstáculo que esté delante y a menos de `radius + margin`.
/// `strength` es la magnitud del empuje. Un obstáculo justo en la línea de avance
/// (componente lateral nula) no añade empuje.
template <class S>
[[nodiscard]] constexpr SteerVec<S> avoid_circles(const SteerVec<S>& pos,
						  const SteerVec<S>& desired,
						  eng::Span<const SteerCircle<S>> obstacles,
						  S margin, S strength) {
	const S zero = eng::math::scalar_traits<S>::zero();
	SteerVec<S> steer = desired;
	for (eng::usize i = 0; i < obstacles.size(); ++i) {
		const SteerVec<S> rel = obstacles[i].center - pos;
		const S limit = obstacles[i].radius + margin;
		if (!(eng::math::length(rel) < limit)) {
			continue; // fuera de alcance
		}
		const S along = eng::math::dot(desired, rel);
		if (!(zero < along)) {
			continue; // el obstáculo está detrás (o perpendicular)
		}
		const SteerVec<S> okdir = eng::math::normalize(rel);
		const SteerVec<S> lateral = eng::math::reject(okdir, desired);
		steer = steer + eng::math::vscale(eng::math::normalize(lateral), strength);
	}
	return steer;
}

} // namespace eng::ai
