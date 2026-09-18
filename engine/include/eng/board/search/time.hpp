#pragma once

/// \file time.hpp
/// **Gestión de tiempo** del buscador: decide cuándo parar según nodos y milisegundos,
/// sin `busy-wait` y sin depender del reloj de una plataforma concreta.
///
/// El reloj se inyecta como función (`NowMsFn`) porque el Amiga no tiene `chrono`:
/// el juego pasa un lector de VBlank/CIA (o de `eng::time`) y el host uno de
/// `std::chrono`. Así el mismo motor sirve en ambos targets y el test puede usar un
/// reloj falso determinista.
///
/// El buscador ya es interrumpible (`Limits::max_nodes` + `StopToken`); este tipo es
/// el que el bucle de juego usa para **pedir** la parada a tiempo (soft = planifica
/// la siguiente jugada; hard = límite absoluto).
///
/// Verificación: HOST-148.

#include <eng/core/types.hpp>

namespace eng::board {

/// Lector de tiempo en milisegundos (monótono). `user` es del llamador.
using NowMsFn = u32 (*)(void* user);

/// Presupuesto de una búsqueda.
struct TimeBudget {
	u64 max_nodes = 0u; ///< 0 = sin límite de nodos
	u32 soft_ms = 0u;   ///< objetivo (permite acabar el ply en curso)
	u32 hard_ms = 0u;   ///< límite absoluto (corta ya)
};

class TimeManager {
public:
	TimeManager() noexcept = default;
	TimeManager(NowMsFn now, void* user) noexcept : m_now(now), m_user(user) {}

	/// Marca el inicio de la búsqueda.
	void start() noexcept { m_start = read_now(); }

	[[nodiscard]] u32 elapsed_ms() const noexcept {
		const u32 now = read_now();
		return (now >= m_start) ? (now - m_start) : 0u;
	}

	/// ¿Hay que parar ya? (límite duro o de nodos).
	[[nodiscard]] bool hard_expired(const TimeBudget& budget, u64 nodes) const noexcept {
		if (budget.max_nodes != 0u && nodes >= budget.max_nodes) {
			return true;
		}
		return budget.hard_ms != 0u && elapsed_ms() >= budget.hard_ms;
	}

	/// ¿Conviene no empezar otra profundidad? (límite blando).
	[[nodiscard]] bool soft_expired(const TimeBudget& budget, u64 nodes) const noexcept {
		if (budget.max_nodes != 0u && nodes >= budget.max_nodes) {
			return true;
		}
		return budget.soft_ms != 0u && elapsed_ms() >= budget.soft_ms;
	}

private:
	[[nodiscard]] u32 read_now() const noexcept { return m_now != nullptr ? m_now(m_user) : 0u; }

	NowMsFn m_now = nullptr;
	void* m_user = nullptr;
	u32 m_start = 0u;
};

} // namespace eng::board
