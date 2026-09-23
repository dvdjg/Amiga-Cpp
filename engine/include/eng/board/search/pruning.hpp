#pragma once

/// \file pruning.hpp
/// Políticas de **poda** del buscador adversario. Hoy: el **null-move pruning**.
///
/// Idea: en posiciones tranquilas (sin jaque, sin final) conviene "pasar turno"
/// (jugada nula) y buscar con profundidad reducida; si aun así la posición supera
/// `beta`, el bando al turno tiene una amenaza tan fuerte que la rama se corta. Es
/// una heurística, no una demostración: se desactiva en finales y en jaque para
/// evitar zugzwang y falsos positivos.
///
/// Coste de memoria nulo. En 68000 se activa solo cuando hay presupuesto suficiente
/// (perfil `P256+`), porque exige nodos y profundidad.
///
/// Verificación: HOST-148.

#include <eng/core/types/types.hpp>

namespace eng::board {

struct NullMoveConfig {
	bool enabled = true;
	u32 min_depth = 3u;   ///< no intentar null-move por debajo de esta profundidad
	u32 reduction = 2u;   ///< R: profundidad que se resta a la búsqueda nula

	/// Condiciones para intentar el null-move.
	[[nodiscard]] constexpr bool allowed(u32 depth, bool in_check, bool endgame) const noexcept {
		return enabled && !in_check && !endgame && depth >= min_depth;
	}
};

} // namespace eng::board
