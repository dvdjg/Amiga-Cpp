#pragma once

/// \file opening_book.hpp
/// **Líneas de apertura** incorporadas: un conjunto pequeño y curado que comparten la
/// demo `123_chess_match` y la simulación host `tools/board/selfplay.cpp`. Construye
/// `BookEntry` (ordenados por clave Zobrist) a partir de líneas UCI y expone el pool
/// de nombres legibles.
///
/// Sin heap ni I/O: el llamador aporta el almacenamiento. La demo reserva 64 entradas
/// en estática; la simulación host puede usar más.
///
/// Verificación: HOST-187 y demo `123_chess_match`.

#include <eng/board/knowledge/book.hpp>
#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/movegen.hpp>
#include <eng/board/rules/chess/notation.hpp>
#include <eng/board/rules/chess/opening.hpp>
#include <eng/core/util/text.hpp>

namespace eng::board::chess {

/// Pool NUL-separado de nombres de apertura (índice = `name_id`).
inline constexpr char kOpeningNames[] =
    "Apertura abierta\0"
    "Defensa Siciliana\0"
    "Defensa Francesa\0"
    "Gambito de Dama\0"
    "India de Rey\0"
    "Apertura Inglesa\0"
    "Apertura Reti\0";

struct OpeningLine {
	const char* uci;
	eng::u16 name_id;
};

/// Líneas del libro (todas desde la posición inicial).
inline constexpr OpeningLine kOpeningLines[] = {
    {"e2e4 e7e5", 0u},      {"e2e4 c7c5", 1u}, {"e2e4 e7e6", 2u},
    {"d2d4 d7d5 c2c4", 3u}, {"d2d4 g8f6", 4u}, {"c2c4", 5u},
    {"g1f3", 6u},
};

/// Jugada legal de `pos` cuyo UCI coincide con `uci` (o `kNoMove`).
[[nodiscard]] inline Move find_move_uci(const Position& pos, eng::util::StringView uci) noexcept {
	MoveList legal;
	generate_legal(pos, legal);
	for (eng::usize i = 0u; i < legal.size(); ++i) {
		char text[8];
		const eng::usize n = to_uci(legal[i], {text, sizeof(text)});
		if (eng::util::StringView {text, n} == uci) {
			return legal[i];
		}
	}
	return kNoMove;
}

/// Construye el libro en `storage` (lo ordena) y devuelve el número de entradas.
[[nodiscard]] inline eng::u32 build_opening_book(eng::Span<BookEntry> storage) noexcept {
	BookBuilder builder {storage};
	for (const OpeningLine& line : kOpeningLines) {
		Position pos;
		set_start(pos);
		eng::util::StringView rest {line.uci};
		while (!rest.empty()) {
			const eng::util::StringView uci = eng::util::split_next(rest, ' ');
			if (uci.empty()) {
				break;
			}
			const Move move = find_move_uci(pos, uci);
			if (move_none(move)) {
				break;
			}
			(void)builder.add(pos.key, move, 10, line.name_id);
			Undo undo;
			make_move(pos, move, undo);
		}
	}
	builder.finalize();
	return builder.size();
}

/// Nombre de apertura legible para `id` (o vista vacía si se sale del pool).
[[nodiscard]] inline eng::util::StringView opening_name(eng::u16 id) noexcept {
	return book_name({kOpeningNames, sizeof(kOpeningNames)}, id);
}

} // namespace eng::board::chess
