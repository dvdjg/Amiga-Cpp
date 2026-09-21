#pragma once

/// \file crowd.hpp
/// **Crowd** (`eng::ai`): actualiza un conjunto de agentes con **separación**, **evasión de
/// obstáculos** y un vector **deseado** (del path, del flow field o de un `seek`), e integra
/// posición y velocidad. Es el orquestador de movimiento local que faltaba sobre
/// `steering.hpp`.
///
/// **Genérico sobre el escalar `S`** (como `steering.hpp`): vale para `float`/`double`
/// (host/tests), fixed-point (`q12`, `Fixed<...>`) y enteros (`s16`/`s32`) siempre que el
/// escalar tenga `scalar_traits` y `scalar_sqrt` (el engine ya los aporta para float, double,
/// fixed y enteros). Las posiciones/velocidades son `eng::math::Vec<2,S>`.
///
/// La fase amplia (vecinos) es una **política** de plantilla para no atar el algoritmo a una
/// rejilla concreta: el llamador elige `BruteForceBroadphase` (cualquier `S`) o
/// `SpatialHashBroadphase` (rejilla uniforme `s16`, la que evita el `O(N²)` en Amiga). Los
/// agentes viven en un pool externo (`Span<CrowdAgent<S>>`), sin heap.
///
/// Uso:
///   eng::ai::Crowd<eng::s32, eng::ai::SpatialHashBroadphase<16, 20, 16, 64>> crowd;
///   crowd.update(agents, params, /*dt=*/1);          // dt en la unidad del escalar (1 tick)
///   const eng::u32 checks = crowd.neighbor_checks(); // trabajo real de vecinos
///
/// Verificación: HOST-249.

#include <eng/core/arith.hpp>
#include <eng/core/geometry.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/broadphase.hpp>
#include <eng/core/util/collision.hpp>

namespace eng::ai {

/// Agente del crowd (POD, sin heap), genérico sobre el escalar `S`. `desired` es una **velocidad
/// deseada** (del path/flow/seek).
template <class S>
struct CrowdAgent {
	eng::u16 id = 0;                  ///< identificador del juego (la fase amplia usa el índice)
	eng::math::Vec<2, S> position {}; ///< posición en el mundo
	eng::math::Vec<2, S> velocity {}; ///< velocidad actual
	eng::math::Vec<2, S> desired {};  ///< velocidad deseada
	S radius {};                      ///< radio de colisión
	S max_speed {};                   ///< velocidad máxima
	eng::u8 layer = 0;                ///< capas distintas no se repelen
	eng::u8 flags = 1;                ///< bit 0 = activo

	[[nodiscard]] constexpr bool active() const noexcept { return (flags & 1u) != 0u; }
};

/// Parámetros del crowd (tunables; mismo escalar que las coordenadas).
template <class S>
struct CrowdParams {
	S separation_radius {}; ///< radio extra de separación entre agentes
	S separation_weight {}; ///< peso de la separación
	S obstacle_weight {};   ///< peso de la evasión de obstáculos
	S look_ahead {};        ///< margen de detección de obstáculos
	S max_force {};         ///< límite de la fuerza de steering
};

/// **Fase amplia por fuerza bruta** (genérica sobre `S`): recorre todas las entradas en cada
/// consulta. Útil para tests, crowds pequeños o escalares que no encajen en una rejilla `s16`.
template <class S, eng::u16 MaxItems>
class BruteForceBroadphase {
public:
	constexpr void clear() noexcept { m_count = 0u; }
	constexpr bool insert(eng::u16 id, const eng::math::Vec<2, S>& p) noexcept {
		if (m_count >= MaxItems) {
			return false;
		}
		m_ids[m_count] = id;
		m_pos[m_count] = p;
		++m_count;
		return true;
	}
	template <class Fn>
	constexpr void for_each_near(const eng::math::Vec<2, S>&, S, Fn fn) const {
		for (eng::u16 i = 0u; i < m_count; ++i) {
			fn(m_ids[i], m_pos[i]);
		}
	}

private:
	eng::u16 m_ids[MaxItems] {};
	eng::math::Vec<2, S> m_pos[MaxItems] {};
	eng::u16 m_count = 0u;
};

/// **Fase amplia por rejilla uniforme** (`s16`): adapta `eng::util::SpatialHash`. Las posiciones
/// `Vec<2,S>` se convierten a coordenadas de rejilla con `scalar_traits<S>::to_int`. Es la que
/// hace que el coste dependa de la densidad local y **no de `N²`**.
template <eng::u16 CellSize, eng::u16 CellsX, eng::u16 CellsY, eng::u16 MaxItems>
class SpatialHashBroadphase {
public:
	constexpr void clear() noexcept { m_grid.clear(); }
	template <class S>
	constexpr bool insert(eng::u16 id, const eng::math::Vec<2, S>& p) noexcept {
		return m_grid.insert(id, to_cell(p.x()), to_cell(p.y()));
	}
	template <class S, class Fn>
	constexpr void for_each_near(const eng::math::Vec<2, S>& center, S reach, Fn fn) const {
		const eng::s16 r = to_cell(reach);
		const eng::s16 cx = to_cell(center.x());
		const eng::s16 cy = to_cell(center.y());
		const eng::util::Aabb box {
			static_cast<eng::s16>(cx - r), static_cast<eng::s16>(cy - r),
			static_cast<eng::s16>(cx + r), static_cast<eng::s16>(cy + r)};
		m_grid.for_each_in(box, [&](eng::u16 id, eng::s16 x, eng::s16 y) {
			fn(id, eng::math::Vec<2, S> {{eng::math::scalar_traits<S>::from_int(x),
						      eng::math::scalar_traits<S>::from_int(y)}});
		});
	}

private:
	/// Convierte una coordenada escalar a coordenada de rejilla `s16` (con `scalar_traits`).
	template <class S>
	[[nodiscard]] static constexpr eng::s16 to_cell(S v) noexcept {
		return static_cast<eng::s16>(eng::math::scalar_traits<S>::to_int(v));
	}

	eng::util::SpatialHash<CellSize, CellsX, CellsY, MaxItems> m_grid {};
};

/// **Crowd** genérico sobre el escalar `S` y la fase amplia `Broadphase` (ver el fichero).
template <class S, class Broadphase>
class Crowd {
public:
	/// Actualiza todos los agentes activos (sin obstáculos). Devuelve el nº de **comprobaciones de
	/// vecino** hechas (para medir el ahorro frente a `N²`).
	[[nodiscard]] eng::u32 update(eng::Span<CrowdAgent<S>> agents, const CrowdParams<S>& p,
				      S dt) noexcept {
		return update(agents, eng::Span<const eng::math::Vec<2, S>> {},
			      eng::Span<const S> {}, p, dt);
	}

	/// Igual, con obstáculos estáticos (posiciones + radios, mismo tamaño). `dt` en la unidad del
	/// juego (p. ej. 8.8 → 256 = 1).
	[[nodiscard]] eng::u32 update(eng::Span<CrowdAgent<S>> agents,
				      eng::Span<const eng::math::Vec<2, S>> obstacles,
				      eng::Span<const S> obstacle_radii, const CrowdParams<S>& p,
				      S dt) noexcept {
		rebuild(agents);
		m_checks = 0u;
		for (eng::u16 i = 0u; i < agents.size(); ++i) {
			CrowdAgent<S>& a = agents[i];
			if (!a.active()) {
				continue;
			}
			const eng::math::Vec<2, S> sep = separation(agents, i, a, p);
			const eng::math::Vec<2, S> obs = avoid(obstacles, obstacle_radii, a, p);
			integrate(a, sep, obs, p, dt);
		}
		return m_checks;
	}

	/// Nº de comprobaciones de vecino de la última `update` (diagnóstico/medición).
	[[nodiscard]] constexpr eng::u32 neighbor_checks() const noexcept { return m_checks; }

private:
	/// Reconstruye la fase amplia con las posiciones actuales y calcula el radio máximo.
	void rebuild(eng::Span<CrowdAgent<S>> agents) noexcept {
		m_broad.clear();
		m_max_radius = eng::math::scalar_traits<S>::zero();
		for (eng::u16 i = 0u; i < agents.size(); ++i) {
			if (!agents[i].active()) {
				continue;
			}
			(void)m_broad.insert(i, agents[i].position);
			if (m_max_radius < agents[i].radius) {
				m_max_radius = agents[i].radius;
			}
		}
	}

	/// Suma de repulsión de los vecinos dentro de `radius` (consulta por la fase amplia). El cajón
	/// de consulta usa el **radio máximo** de los agentes, para no dejar fuera a un vecino cuyo
	/// radio haga que el umbral de separación supere `a.radius + separation_radius`.
	[[nodiscard]] eng::math::Vec<2, S> separation(eng::Span<CrowdAgent<S>> agents, eng::u16 self,
						      const CrowdAgent<S>& a,
						      const CrowdParams<S>& p) noexcept {
		const S reach = a.radius + m_max_radius + p.separation_radius;
		eng::math::Vec<2, S> push {};
		m_broad.for_each_near(a.position, reach, [&](eng::u16 id, const eng::math::Vec<2, S>& bp) {
			++m_checks;
			if (id == self) {
				return;
			}
			const CrowdAgent<S>& b = agents[id];
			if (!b.active() || b.layer != a.layer) {
				return;
			}
			const eng::math::Vec<2, S> d = a.position - bp;
			const S rr = a.radius + b.radius + p.separation_radius;
			if (eng::math::length_sq(d) < eng::math::mul_norm(rr, rr)) {
				push = push + d;
			}
		});
		return push;
	}

	/// Suma de repulsión de los obstáculos dentro de `radius + look_ahead`.
	[[nodiscard]] static eng::math::Vec<2, S> avoid(eng::Span<const eng::math::Vec<2, S>> obstacles,
							eng::Span<const S> radii,
							const CrowdAgent<S>& a,
							const CrowdParams<S>& p) noexcept {
		const eng::usize n = obstacles.size() < radii.size() ? obstacles.size() : radii.size();
		eng::math::Vec<2, S> push {};
		for (eng::usize k = 0u; k < n; ++k) {
			const eng::math::Vec<2, S> d = a.position - obstacles[k];
			const S rr = a.radius + radii[k] + p.look_ahead;
			if (eng::math::length_sq(d) < eng::math::mul_norm(rr, rr)) {
				push = push + d;
			}
		}
		return push;
	}

	/// Combina deseado + separación + obstáculos, limita la fuerza, suaviza, limita la velocidad e
	/// integra la posición. `dt` va en la unidad del escalar (1 = un tick para escalares enteros;
	/// una fracción para fixed/float).
	static void integrate(CrowdAgent<S>& a, const eng::math::Vec<2, S>& sep,
			      const eng::math::Vec<2, S>& obs, const CrowdParams<S>& p, S dt) noexcept {
		eng::math::Vec<2, S> force = a.desired +
					     eng::math::vscale(sep, p.separation_weight) +
					     eng::math::vscale(obs, p.obstacle_weight);
		limit(force, p.max_force);

		// Suavizado `(v*3 + f) / 4` componente a componente: con escalares enteros, dividir por
		// `div_norm(1,4)` (que sería 0) rompería; dividir el numerador sí es correcto.
		const S three = eng::math::scalar_traits<S>::from_int(3);
		const S four = eng::math::scalar_traits<S>::from_int(4);
		eng::math::Vec<2, S> vel = eng::math::vscale(a.velocity, three) + force;
		vel.x() = eng::math::div_norm(vel.x(), four);
		vel.y() = eng::math::div_norm(vel.y(), four);
		limit(vel, a.max_speed);
		a.velocity = vel;
		a.position = a.position + eng::math::vscale(a.velocity, dt);
	}

	/// Escala `v` si su módulo supera `max`. Escala **componente a componente** (`(v*max)/len`):
	/// con escalares enteros, `div_norm(max,len)` truncaría a 0 cuando `max < len`.
	static void limit(eng::math::Vec<2, S>& v, S max) noexcept {
		const S len2 = eng::math::length_sq(v);
		const S max2 = eng::math::mul_norm(max, max);
		if (!(max2 < len2)) {
			return;
		}
		const S len = eng::math::length(v);
		if (len == eng::math::scalar_traits<S>::zero()) {
			v = {};
			return;
		}
		v.x() = eng::math::div_norm(eng::math::mul_norm(v.x(), max), len);
		v.y() = eng::math::div_norm(eng::math::mul_norm(v.y(), max), len);
	}

	Broadphase m_broad {};
	eng::u32 m_checks = 0u;
	S m_max_radius {}; ///< radio máximo de los agentes activos (cota del cajón de consulta)
};

} // namespace eng::ai
