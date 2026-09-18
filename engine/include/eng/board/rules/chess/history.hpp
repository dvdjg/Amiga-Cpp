#pragma once

/// \file history.hpp
/// Historial de posiciones de una partida de ajedrez y detección de la **regla de
/// las tres repeticiones**.
///
/// Cómo funciona la repetición: la clave Zobrist de una posición incluye pieza,
/// turno, derechos de enroque y casilla al paso, así que dos posiciones son la
/// misma solo si coinciden en todo eso. Una repetición **no puede cruzar** un
/// movimiento irreversible (captura o peón), porque a partir de ahí la posición es
/// distinta; por eso basta mirar hacia atrás la ventana de la regla de los 50
/// movimientos (`halfmove + 1` entradas), no toda la partida.
///
/// Uso en partida:
///   Position pos; set_start(pos);
///   PositionHistory<512> history; history.push(pos.key);
///   ... make_move(pos, m, undo); history.push(pos.key); ...
///
/// Verificación: HOST-141.

#include <eng/board/core/types.hpp>

namespace eng::board::chess {

/// Historial circular de claves Zobrist. Capacidad fija (`MaxMoves`), sin heap.
template <eng::u32 MaxMoves>
class PositionHistory {
public:
	PositionHistory() noexcept { reset(); }
	PositionHistory(const PositionHistory&) = delete;
	PositionHistory& operator=(const PositionHistory&) = delete;

	void reset() noexcept { m_count = 0u; }

	/// Registra la clave de la posición actual (tras fijarla o tras una jugada).
	void push(eng::u32 key) noexcept {
		if (m_count < MaxMoves) {
			m_keys[m_count] = key;
			++m_count;
		} else {
			// Historial lleno: descarta la entrada más antigua (la repetición
			// relevante está siempre en la ventana reciente).
			for (eng::u32 i = 1u; i < MaxMoves; ++i) {
				m_keys[i - 1u] = m_keys[i];
			}
			m_keys[MaxMoves - 1u] = key;
		}
	}

	/// Deshace el último registro (para el `unmake` de la búsqueda).
	void pop() noexcept {
		if (m_count != 0u) {
			--m_count;
		}
	}

	[[nodiscard]] eng::u32 size() const noexcept { return m_count; }
	[[nodiscard]] eng::u32 capacity() const noexcept { return MaxMoves; }

	/// ¿Cuántas veces aparece `key` en las últimas `lookback` entradas (la actual
	/// incluida)? Con `lookback = halfmove + 1` respeta la ventana de los 50
	/// movimientos.
	[[nodiscard]] eng::u32 repetitions(eng::u32 key, eng::u32 lookback) const noexcept {
		const eng::u32 window = (lookback < m_count) ? lookback : m_count;
		eng::u32 count = 0u;
		for (eng::u32 i = 0u; i < window; ++i) {
			if (m_keys[m_count - 1u - i] == key) {
				++count;
			}
		}
		return count;
	}

private:
	eng::u32 m_keys[MaxMoves] {};
	eng::u32 m_count = 0u;
};

} // namespace eng::board::chess
