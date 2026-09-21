#pragma once

/// \file crowd.hpp
/// **Crowd** (`eng::ai`): actualiza un conjunto de agentes con **separación**, **evasión de
/// obstáculos** y un vector **deseado** (del path, del flow field o de un `seek`), e integra
/// posición y velocidad. Es el orquestador de movimiento local que faltaba sobre
/// `steering.hpp`.
///
/// La clave para Amiga es **no comparar todos contra todos**: los vecinos se buscan con la
/// **rejilla espacial** `eng::util::SpatialHash` (fase amplia de colisiones ya existente), de
/// modo que el coste depende de la densidad local, no de `N²`. Los agentes viven en un pool
/// que aporta el llamador (`Span<CrowdAgent>`), sin heap.
///
/// Aritmética **entera** (`s16` posiciones, `s32` intermedios): el mundo de navegación usa
/// `Point2s`, mientras que `steering.hpp` es genérico sobre el escalar (`q12`/`float`) para
/// velocidades normalizadas. Aquí se replican las conductas (separación, evasión, límite de
/// fuerza/velocidad) en el dominio entero, con **una** raíz (`isqrt`) por agente y ninguna
/// división por vecino.
///
/// Uso:
///   eng::ai::Crowd<16, 20, 16, 64> crowd;
///   crowd.update(agents, params, /*dt=*/256);           // dt en 8.8 (256 = 1 tick)
///   const eng::u32 checks = crowd.neighbor_checks();     // para medir el ahorro vs O(N²)
///
/// Verificación: HOST-249.

#include <eng/core/isqrt.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/broadphase.hpp>
#include <eng/core/util/collision.hpp>

namespace eng::ai {

/// Agente del crowd (POD, sin heap). `desired` es una **velocidad deseada** (del path/flow/seek).
struct CrowdAgent {
	eng::u16 id = 0;            ///< identificador del juego (informativo; la rejilla usa el índice)
	eng::Point2s position {};   ///< posición en el mundo (píxeles)
	eng::Point2s velocity {};   ///< velocidad actual (px/tick, escala del juego)
	eng::Point2s desired {};    ///< velocidad deseada (px/tick)
	eng::s16 radius = 6;        ///< radio de colisión
	eng::s16 max_speed = 24;    ///< velocidad máxima (px/tick)
	eng::u8 layer = 0;          ///< capas distintas no se repelen
	eng::u8 flags = 1;          ///< bit 0 = activo

	[[nodiscard]] constexpr bool active() const noexcept { return (flags & 1u) != 0u; }
};

/// Parámetros del crowd (tunables; mismos nombres que la propuesta de diseño).
struct CrowdParams {
	eng::s16 separation_radius = 18; ///< radio extra de separación entre agentes
	eng::s16 separation_weight = 40; ///< peso de la separación (escala 64 = 1.0)
	eng::s16 obstacle_weight = 50;   ///< peso de la evasión de obstáculos (escala 64)
	eng::s16 look_ahead = 12;        ///< margen de detección de obstáculos
	eng::s16 max_force = 32;         ///< límite de la fuerza de steering (px/tick)
};

/// **Crowd** con rejilla de vecinos. `CellSize` (potencia de dos) debe ser del orden del radio de
/// separación; `CellsX×CellsY` debe cubrir el mundo; `MaxItems` ≥ nº de agentes.
template <eng::u16 CellSize, eng::u16 CellsX, eng::u16 CellsY, eng::u16 MaxItems>
class Crowd {
public:
	/// Actualiza todos los agentes activos (sin obstáculos). Devuelve el nº de **comprobaciones de
	/// vecino** hechas (para medir el ahorro frente a `N²`).
	[[nodiscard]] eng::u32 update(eng::Span<CrowdAgent> agents, const CrowdParams& p,
				      eng::s16 dt) noexcept {
		return update(agents, eng::Span<const eng::Point2s> {}, eng::Span<const eng::s16> {}, p, dt);
	}

	/// Igual, con obstáculos estáticos (posiciones + radios, mismo tamaño). `dt` en 8.8 (256 = 1).
	[[nodiscard]] eng::u32 update(eng::Span<CrowdAgent> agents,
				      eng::Span<const eng::Point2s> obstacles,
				      eng::Span<const eng::s16> obstacle_radii, const CrowdParams& p,
				      eng::s16 dt) noexcept {
		rebuild(agents);
		m_checks = 0u;
		for (eng::u16 i = 0u; i < agents.size(); ++i) {
			CrowdAgent& a = agents[i];
			if (!a.active()) {
				continue;
			}
			const eng::Point2s sep = separation(agents, i, a, p);
			const eng::Point2s obs = avoid(obstacles, obstacle_radii, a, p);
			integrate(a, sep, obs, p, dt);
		}
		return m_checks;
	}

	/// Nº de comprobaciones de vecino de la última `update` (diagnóstico/medición).
	[[nodiscard]] constexpr eng::u32 neighbor_checks() const noexcept { return m_checks; }

private:
	/// Reconstruye la fase amplia con las posiciones actuales de los agentes activos.
	void rebuild(eng::Span<CrowdAgent> agents) noexcept {
		m_grid.clear();
		for (eng::u16 i = 0u; i < agents.size(); ++i) {
			if (agents[i].active()) {
				(void)m_grid.insert(i, agents[i].position.x, agents[i].position.y);
			}
		}
	}

	/// Suma de repulsión de los vecinos dentro de `radius` (consulta por la rejilla, no `N²`).
	[[nodiscard]] eng::Point2s separation(eng::Span<CrowdAgent> agents, eng::u16 self,
					      const CrowdAgent& a, const CrowdParams& p) noexcept {
		const eng::s16 reach = static_cast<eng::s16>(a.radius + p.separation_radius);
		const eng::util::Aabb box {
			static_cast<eng::s16>(a.position.x - reach),
			static_cast<eng::s16>(a.position.y - reach),
			static_cast<eng::s16>(a.position.x + reach),
			static_cast<eng::s16>(a.position.y + reach)};
		eng::s32 sx = 0, sy = 0;
		m_grid.for_each_in(box, [&](eng::u16 id, eng::s16 bx, eng::s16 by) {
			++m_checks;
			if (id == self) {
				return;
			}
			const CrowdAgent& b = agents[id];
			if (!b.active() || b.layer != a.layer) {
				return;
			}
			const eng::s32 dx = static_cast<eng::s32>(a.position.x) - bx;
			const eng::s32 dy = static_cast<eng::s32>(a.position.y) - by;
			const eng::s32 rr = a.radius + b.radius + p.separation_radius;
			if (dx * dx + dy * dy < rr * rr) {
				sx += dx;
				sy += dy;
			}
		});
		return eng::Point2s {static_cast<eng::s16>(sx), static_cast<eng::s16>(sy)};
	}

	/// Suma de repulsión de los obstáculos dentro de `radius + look_ahead`.
	[[nodiscard]] static eng::Point2s avoid(eng::Span<const eng::Point2s> obstacles,
						eng::Span<const eng::s16> radii,
						const CrowdAgent& a, const CrowdParams& p) noexcept {
		const eng::usize n = obstacles.size() < radii.size() ? obstacles.size() : radii.size();
		eng::s32 ox = 0, oy = 0;
		for (eng::usize k = 0u; k < n; ++k) {
			const eng::s32 dx = static_cast<eng::s32>(a.position.x) - obstacles[k].x;
			const eng::s32 dy = static_cast<eng::s32>(a.position.y) - obstacles[k].y;
			const eng::s32 rr = a.radius + radii[k] + p.look_ahead;
			if (dx * dx + dy * dy < rr * rr) {
				ox += dx;
				oy += dy;
			}
		}
		return eng::Point2s {static_cast<eng::s16>(ox), static_cast<eng::s16>(oy)};
	}

	/// Combina deseado + separación + obstáculos, limita la fuerza, suaviza, limita la velocidad e
	/// integra la posición. Aritmética `s32` con una raíz (`isqrt`) por límite.
	static void integrate(CrowdAgent& a, eng::Point2s sep, eng::Point2s obs, const CrowdParams& p,
			      eng::s16 dt) noexcept {
		eng::s32 fx = a.desired.x + ((static_cast<eng::s32>(sep.x) * p.separation_weight) >> 6) +
			      ((static_cast<eng::s32>(obs.x) * p.obstacle_weight) >> 6);
		eng::s32 fy = a.desired.y + ((static_cast<eng::s32>(sep.y) * p.separation_weight) >> 6) +
			      ((static_cast<eng::s32>(obs.y) * p.obstacle_weight) >> 6);
		limit(fx, fy, p.max_force);

		eng::s32 vx = (static_cast<eng::s32>(a.velocity.x) * 3 + fx) / 4;
		eng::s32 vy = (static_cast<eng::s32>(a.velocity.y) * 3 + fy) / 4;
		limit(vx, vy, a.max_speed);
		a.velocity.x = static_cast<eng::s16>(vx);
		a.velocity.y = static_cast<eng::s16>(vy);

		a.position.x = static_cast<eng::s16>(a.position.x +
						     (static_cast<eng::s32>(a.velocity.x) * dt) / 256);
		a.position.y = static_cast<eng::s16>(a.position.y +
						     (static_cast<eng::s32>(a.velocity.y) * dt) / 256);
	}

	/// Escala `(x,y)` si su módulo supera `max` (una `isqrt`; sin división si no hace falta).
	static void limit(eng::s32& x, eng::s32& y, eng::s32 max) noexcept {
		const eng::s32 len2 = x * x + y * y;
		if (len2 <= max * max) {
			return;
		}
		const eng::s32 len = static_cast<eng::s32>(eng::isqrt(static_cast<eng::u32>(len2)));
		if (len == 0) {
			x = 0;
			y = 0;
			return;
		}
		x = (x * max) / len;
		y = (y * max) / len;
	}

	eng::util::SpatialHash<CellSize, CellsX, CellsY, MaxItems> m_grid {};
	eng::u32 m_checks = 0u;
};

} // namespace eng::ai
