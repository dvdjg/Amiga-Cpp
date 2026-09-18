#pragma once

/// \file opening.hpp
/// Puente entre el **libro de aperturas** (`eng::board::BookEntry`) y una posición de
/// ajedrez: consulta el libro por la clave Zobrist de la posición.
///
/// Verificación: HOST-157.

#include <eng/board/knowledge/book.hpp>
#include <eng/board/rules/chess/board.hpp>

namespace eng::board::chess {

/// Consulta el libro de aperturas para `pos` (búsqueda binaria por clave).
[[nodiscard]] inline BookProbe probe_opening_book(const Position& pos,
                                                  eng::Span<const BookEntry> book) noexcept {
	return probe_book(book, pos.key);
}

} // namespace eng::board::chess
