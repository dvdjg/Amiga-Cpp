#pragma once

/// \file physics3d.hpp
/// **Física 3D arcade mínima** (G7 parcial de `ROADMAP_3D_GAME_API.md`): cuerpos
/// esféricos con gravedad, suelo plano, un impulso por contacto, fricción
/// saturada, reposo (`sleep`) y escala temporal. Sin solver iterativo, sin OBB,
/// sin broadphase y sin heap: el primer corte para un jugador-cápsula/props.
///
/// Genérica sobre el escalar `S` (patrón `eng::math::scalar_traits`): el
/// llamador elige `float` en host o un `Fixed` de rango suficiente en 68000.
/// El integrador es **semi-implícito** y `dt` se pasa por el llamador (normalmente
/// un tick fijo de 1/50 s); `time_scale` permite el tiempo lento de *Superhot*.
///
/// ```text
///   gravity + scale → integrar → suelo → pares esfera-esfera → sleep
/// ```

#include <eng/core/math/geometry.hpp>
#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/types/types.hpp>

namespace eng::sim3d {

/// Cuerpo esférico. `inv_mass == 0` = estático (no se mueve ni recibe impulsos).
/// \tparam S escalar de la operación.
template <class S>
struct Body3D {
	eng::math::Vec<3, S> position {}; ///< centro (unidades de mundo).
	eng::math::Vec<3, S> velocity {}; ///< velocidad (unidades/s).
	S radius {}; ///< radio >= 0.
	S inv_mass {}; ///< 1/masa; 0 = estático.
	bool active = true; ///< si es `false`, no participa.
	bool sleeping = false; ///< reposo: no se integra hasta recibir un golpe.
};

/// **Mundo de cuerpos esféricos de capacidad fija**.
/// \tparam S escalar de posición/velocidad (mismo para todos los cuerpos).
/// \tparam N número máximo de cuerpos; > 0.
template <class S, eng::u16 N>
class PhysicsWorld3D {
	static_assert(N > 0u, "PhysicsWorld3D: N debe ser > 0");

public:
	using Body = Body3D<S>;

	/// \return número de cuerpos registrados.
	[[nodiscard]] constexpr eng::u16 count() const noexcept { return m_count; }
	/// \return capacidad de cuerpos.
	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return N; }
	/// Acceso al cuerpo `i` (sin comprobar rango; el llamador usa `count()`).
	[[nodiscard]] constexpr Body& body(eng::u16 i) noexcept { return m_body[i]; }
	[[nodiscard]] constexpr const Body& body(eng::u16 i) const noexcept { return m_body[i]; }

	/// Añade un cuerpo al primer hueco.
	/// \param body estado inicial (se copia).
	/// \return índice del cuerpo, o `0xffff` si el mundo está lleno.
	[[nodiscard]] eng::u16 add(const Body& body) noexcept {
		if (m_count >= N) return 0xffffu;
		m_body[m_count] = body;
		return m_count++;
	}

	/// Vacía el mundo.
	void clear() noexcept { m_count = 0u; }

	/// Suelo horizontal `y = y`.
	void set_ground(S y) noexcept { m_ground_y = y; }
	[[nodiscard]] constexpr S ground() const noexcept { return m_ground_y; }
	/// Gravedad (por defecto -9 en unidades/s²).
	void set_gravity(S g) noexcept { m_gravity = g; }
	/// Restitución del rebote con el suelo `[0,1]` (por defecto 0,25).
	void set_restitution(S e) noexcept { m_restitution = e; }
	/// Escala temporal (0 = congelado; 1 = tiempo real).
	void set_time_scale(S scale) noexcept { m_time_scale = scale; }
	[[nodiscard]] constexpr S time_scale() const noexcept { return m_time_scale; }
	/// Umbral de velocidad² bajo el que un cuerpo se duerme.
	void set_sleep_threshold(S v_sq) noexcept { m_sleep_sq = v_sq; }

	/// Avanza la simulación `dt` segundos (se multiplica por `time_scale`).
	/// \param dt paso base; `dt <= 0` no hace nada.
	/// \post Los cuerpos activos no estáticos se integran; el suelo y los pares
	///       esfera-esfera resuelven un impulso; los cuerpos lentos se duermen.
	void step(S dt) noexcept {
		using T = eng::math::scalar_traits<S>;
		const S zero = T::zero();
		const S one = T::one();
		if (!(dt > zero)) return;
		const S h = eng::math::mul_norm(dt, m_time_scale);
		if (!(h > zero)) return;

		for (eng::u16 i = 0u; i < m_count; ++i) {
			Body& b = m_body[i];
			if (!b.active || b.sleeping || b.inv_mass == zero) continue;
			b.velocity.v[1] = b.velocity.v[1] + eng::math::mul_norm(m_gravity, h);
			for (int k = 0; k < 3; ++k) {
				b.position.v[k] = b.position.v[k] + eng::math::mul_norm(b.velocity.v[k], h);
			}
			const S floor = m_ground_y + b.radius;
			if (b.position.v[1] < floor) {
				b.position.v[1] = floor;
				if (b.velocity.v[1] < zero) {
					b.velocity.v[1] = eng::math::mul_norm(-b.velocity.v[1], m_restitution);
					b.velocity.v[0] = eng::math::mul_norm(b.velocity.v[0], one - m_restitution);
					b.velocity.v[2] = eng::math::mul_norm(b.velocity.v[2], one - m_restitution);
				}
			}
			const S v_sq = eng::math::dot(b.velocity, b.velocity);
			if (v_sq <= m_sleep_sq) b.sleeping = true;
		}

		for (eng::u16 i = 0u; i < m_count; ++i) {
			Body& a = m_body[i];
			if (!a.active || a.inv_mass == zero) continue;
			for (eng::u16 j = static_cast<eng::u16>(i + 1u); j < m_count; ++j) {
				Body& b = m_body[j];
				if (!b.active) continue;
				const S r = a.radius + b.radius;
				const eng::math::Vec<3, S> d = b.position - a.position;
				const S d_sq = eng::math::dot(d, d);
				if (d_sq >= eng::math::mul_norm(r, r) || d_sq == zero) continue;
				// Normal aproximada por el centro; d_sq > 0 por la guarda anterior.
				const S dist = eng::math::scalar_sqrt<S>::op(d_sq);
				eng::math::Vec<3, S> n {};
				for (int k = 0; k < 3; ++k) n.v[k] = eng::math::div_norm(d.v[k], dist);
				const S inv_sum = a.inv_mass + b.inv_mass;
				if (inv_sum == zero) continue;
				// Corrección posicional proporcional a la masa inversa.
				const S overlap = r - dist;
				const S push = eng::math::div_norm(overlap, inv_sum);
				for (int k = 0; k < 3; ++k) {
					a.position.v[k] = a.position.v[k] - eng::math::mul_norm(n.v[k], eng::math::mul_norm(push, a.inv_mass));
					b.position.v[k] = b.position.v[k] + eng::math::mul_norm(n.v[k], eng::math::mul_norm(push, b.inv_mass));
				}
				// Un impulso por contacto (sin iterar).
				const eng::math::Vec<3, S> rel = b.velocity - a.velocity;
				const S vn = eng::math::dot(rel, n);
				if (vn < zero) {
					const S e = m_restitution;
					const S jimp = eng::math::div_norm(eng::math::mul_norm(-(one + e), vn), inv_sum);
					for (int k = 0; k < 3; ++k) {
						a.velocity.v[k] = a.velocity.v[k] - eng::math::mul_norm(n.v[k], eng::math::mul_norm(jimp, a.inv_mass));
						b.velocity.v[k] = b.velocity.v[k] + eng::math::mul_norm(n.v[k], eng::math::mul_norm(jimp, b.inv_mass));
					}
				}
				a.sleeping = false;
				b.sleeping = false;
			}
		}
	}

private:
	Body m_body[N] {};
	eng::u16 m_count = 0u;
	S m_ground_y {}; ///< y del suelo.
	S m_gravity = eng::math::scalar_traits<S>::from_int(-9); ///< gravedad.
	S m_restitution = eng::math::div_norm(eng::math::scalar_traits<S>::one(),
					      eng::math::scalar_traits<S>::from_int(4)); ///< 0,25.
	S m_time_scale = eng::math::scalar_traits<S>::one(); ///< 1 = tiempo real.
	S m_sleep_sq = eng::math::div_norm(eng::math::scalar_traits<S>::one(),
					   eng::math::scalar_traits<S>::from_int(64)); ///< reposo.
};

} // namespace eng::sim3d
