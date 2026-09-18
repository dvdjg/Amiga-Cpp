#pragma once

/// \file influence_map.hpp
/// `eng::ai::InfluenceMap<W,H>`: **mapa de influencia** sobre una rejilla (`s32` por
/// celda). Los sistemas **depositan** valores (amenaza, cobertura, interés, control) y
/// el mapa se **degrada** con el tiempo; el agente consulta `at` o `strongest` para
/// elegir hacia dónde ir. Sin heap: el almacenamiento va inline.
///
/// Es una herramienta de percepción/táctica: el mismo dato sirve para huir de zonas
/// peligrosas, concentrarse en un flanco o elegir el punto más "caliente".
///
/// Uso:
///   eng::ai::InfluenceMap<16, 16> danger;
///   danger.deposit(cell, 100);   // amenaza en una celda
///   danger.decay(10);            // el tiempo la difumina
///   const eng::u16 focus = danger.strongest();
///
/// Verificación: HOST-117.

#include <eng/core/types.hpp>

namespace eng::ai {

template <eng::u16 W, eng::u16 H>
class InfluenceMap {
	static_assert(W > 0u && H > 0u, "InfluenceMap: rejilla no vacia");

public:
	static constexpr eng::usize size = static_cast<eng::usize>(W) * H;
	static constexpr eng::u16 no_cell = 0xffffu;

	constexpr void clear() noexcept {
		for (eng::usize i = 0; i < size; ++i) {
			m_values[i] = 0;
		}
	}

	constexpr void set(eng::u16 idx, eng::s32 value) noexcept { m_values[idx] = value; }
	constexpr void deposit(eng::u16 idx, eng::s32 amount) noexcept { m_values[idx] += amount; }

	/// Degrada todo el mapa restando `amount` (nunca por debajo de 0).
	constexpr void decay(eng::s32 amount) noexcept {
		for (eng::usize i = 0; i < size; ++i) {
			const eng::s32 v = m_values[i] - amount;
			m_values[i] = v < 0 ? 0 : v;
		}
	}

	[[nodiscard]] constexpr eng::s32 at(eng::u16 idx) const noexcept { return m_values[idx]; }

	/// Celda con mayor influencia (empate → índice menor); `no_cell` si todas son <= 0.
	[[nodiscard]] constexpr eng::u16 strongest() const noexcept {
		eng::u16 best = no_cell;
		eng::s32 best_v = 0;
		for (eng::usize i = 0; i < size; ++i) {
			if (m_values[i] > best_v) {
				best_v = m_values[i];
				best = static_cast<eng::u16>(i);
			}
		}
		return best;
	}

private:
	eng::s32 m_values[size] {};
};

} // namespace eng::ai
