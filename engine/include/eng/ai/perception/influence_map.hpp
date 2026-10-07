#pragma once

/// \file influence_map.hpp
/// `eng::ai::InfluenceMap<W,H,T>`: **mapa de influencia** sobre una rejilla (un valor
/// `T` por celda, por defecto `s32`). Los sistemas **depositan** valores (amenaza,
/// cobertura, interés, control) y el mapa se **degrada** con el tiempo; el agente
/// consulta `at` o `strongest` para elegir hacia dónde ir. Sin heap: el almacenamiento
/// va inline. El valor es parámetro (`T`, entero con signo): `s16`/`s32` según el rango
/// que necesite el consumidor.
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
/// ```text
///   sistemas (percepción/táctica)        InfluenceMap<W,H> (inline, sin heap)      agente
///   ────────────────────────────         ───────────────────────────────────       ──────
///   deposit(idx, amenaza) ────────────► [ W×H celdas s32 ]
///   decay(amount) cada tick  ─────────► (se difumina; recorta a >= 0)
///                                           │
///                                           ├─ at(idx)      → valor de una celda
///                                           └─ strongest()  → celda más "caliente"
///                                              (empate → índice menor; no_cell si todas <= 0)
///   Reutilizado por sim::Colony como feromonas (Food / Danger / Home / Recruit).
/// ```
///
/// Verificación: HOST-117.

#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::ai {

template <eng::u16 W, eng::u16 H, class T = eng::s32>
class InfluenceMap {
	static_assert(W > 0u && H > 0u, "InfluenceMap: rejilla no vacia");
	static_assert(eng::util::is_integral_v<T> && eng::util::is_signed_v<T>,
		      "InfluenceMap: T debe ser un entero con signo (el decay no baja de 0)");

public:
	static constexpr eng::usize size = static_cast<eng::usize>(W) * H;
	static constexpr eng::u16 no_cell = 0xffffu;

	constexpr void clear() noexcept {
		for (eng::usize i = 0; i < size; ++i) {
			m_values[i] = 0;
		}
	}

	constexpr void set(eng::u16 idx, T value) noexcept { m_values[idx] = value; }
	constexpr void deposit(eng::u16 idx, T amount) noexcept { m_values[idx] += amount; }

	/// Degrada todo el mapa restando `amount` (nunca por debajo de 0).
	constexpr void decay(T amount) noexcept {
		for (eng::usize i = 0; i < size; ++i) {
			const T v = static_cast<T>(m_values[i] - amount);
			m_values[i] = v < 0 ? T {0} : v;
		}
	}

	[[nodiscard]] constexpr T at(eng::u16 idx) const noexcept { return m_values[idx]; }

	/// Celda con mayor influencia (empate → índice menor); `no_cell` si todas son <= 0.
	[[nodiscard]] constexpr eng::u16 strongest() const noexcept {
		eng::u16 best = no_cell;
		T best_v = 0;
		for (eng::usize i = 0; i < size; ++i) {
			if (m_values[i] > best_v) {
				best_v = m_values[i];
				best = static_cast<eng::u16>(i);
			}
		}
		return best;
	}

private:
	T m_values[size] {};
};

} // namespace eng::ai
