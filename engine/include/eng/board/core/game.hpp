#pragma once

/// \file game.hpp
/// Contrato `GameRules` que especializa la búsqueda adversaria de `eng::board` y
/// vocabulario de fin de partida (`Terminal`). El motor de búsqueda (`search/`) se
/// escribe **una vez** contra este contrato; ajedrez y Go solo aportan su policy.
///
/// Un juego cumple `GameRules` si ofrece:
///   - `Position`, `Move`, `MoveList` (tipos);
///   - `initial() -> Position`;
///   - `generate_legal(const Position&, MoveList&) -> nº de jugadas`;
///   - `in_check(const Position&) -> bool` (jaque del bando al turno; en Go, la
///     comprobación análoga que decida el juego);
///   - `zobrist(const Position&) -> u32` (clave de la posición);
///   - `terminal(const Position&) -> Terminal` (fin de partida o `None`).
///
/// Verificación: HOST-138 (concepto y policy de prueba) y HOST-140 (ajedrez real).

#include <eng/core/types.hpp>

namespace eng::board {

/// Estado terminal de una posición, agnóstico del juego concreto.
enum class Terminal : u8 {
	None = 0u,
	Checkmate,
	Stalemate,
	Draw50,
	Repetition,
	InsufficientMaterial,
};

/// Verdadero para cualquier estado que decide la partida.
[[nodiscard]] constexpr bool terminal_is_over(Terminal terminal) noexcept {
	return terminal != Terminal::None;
}

/// Contrato de reglas de un juego de tablero por turnos.
template <class G>
concept GameRules = requires(const typename G::Position& position, typename G::MoveList& moves) {
	typename G::Position;
	typename G::Move;
	typename G::MoveList;
	G::initial();
	G::generate_legal(position, moves);
	G::in_check(position);
	G::zobrist(position);
	G::terminal(position);
};

} // namespace eng::board
