#pragma once

/// \file patterns.hpp
/// **Patrones de apertura (fuseki) de Go 9×9**: un conjunto pequeño de puntos
/// recomendados (hoshi/estrella 4-4 y komoku 3-4) que el motor usa en las primeras
/// jugadas. Es la base de un futuro banco de patrones más rico (3×3/5×5) cargable
/// desde disquete.
///
/// Verificación: HOST-155.

#include <eng/board/rules/go/board.hpp>

namespace eng::board::go {

/// Jugada de apertura recomendada: primer punto estrella (4-4) libre; si no, primer
/// komoku (3-4) libre. Devuelve `kGoPass` si no hay apertura aplicable.
[[nodiscard]] inline Move opening_move(const Position& pos) noexcept {
	static constexpr u8 stars[4] = {make_point(2u, 2u), make_point(6u, 2u), make_point(2u, 6u),
	                                make_point(6u, 6u)};
	for (u8 i = 0u; i < 4u; ++i) {
		if (pos.board[stars[i]] == kEmpty) {
			return go_move(stars[i]);
		}
	}
	static constexpr u8 komoku[8] = {make_point(2u, 3u), make_point(3u, 2u), make_point(6u, 3u),
	                                 make_point(5u, 2u), make_point(2u, 5u), make_point(3u, 6u),
	                                 make_point(6u, 5u), make_point(5u, 6u)};
	for (u8 i = 0u; i < 8u; ++i) {
		if (pos.board[komoku[i]] == kEmpty) {
			return go_move(komoku[i]);
		}
	}
	return kGoPass;
}

} // namespace eng::board::go
