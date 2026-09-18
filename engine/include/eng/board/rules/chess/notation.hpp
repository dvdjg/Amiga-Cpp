#pragma once

/// \file notation.hpp
/// Notación algebraica abreviada **SAN** (y UCI) para ajedrez: convierte una jugada
/// en texto legible. La usan el libro de aperturas, las tablas, los tests y el
/// explicador de partidas.
///
/// SAN incluye el enroque (`O-O`), la captura (`x`), la promoción (`=Q`), la
/// **desambiguación** cuando dos piezas del mismo tipo pueden ir al mismo destino
/// (por columna, fila o ambas) y los sufijos `+` (jaque) y `#` (mate), que exigen
/// aplicar la jugada y comprobar la respuesta.
///
/// Verificación: HOST-142.

#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/movegen.hpp>

namespace eng::board::chess {

/// Letra de figura (mayúscula). El peón no lleva letra en SAN.
[[nodiscard]] constexpr char san_piece_letter(PieceType type) noexcept {
	switch (type) {
	case PieceType::Knight: return 'N';
	case PieceType::Bishop: return 'B';
	case PieceType::Rook: return 'R';
	case PieceType::Queen: return 'Q';
	case PieceType::King: return 'K';
	default: return '?';
	}
}

/// Escribe un carácter si cabe (siempre avanza el contador interno `n`).
inline void san_put(char* out, eng::usize cap, eng::usize& n, char c) noexcept {
	if (n + 1u < cap) {
		out[n] = c;
	}
	++n;
}

/// Escribe la casilla como texto (`e4`).
inline void san_put_square(char* out, eng::usize cap, eng::usize& n, Square square) noexcept {
	san_put(out, cap, n, static_cast<char>('a' + square_file(square)));
	san_put(out, cap, n, static_cast<char>('1' + square_rank(square)));
}

/// Convierte `move` a SAN dentro de `out` (trunca con seguridad). `terminal_suffix`
/// añade `+`/`#` (por defecto sí; desactívalo para el libro, que guarda la jugada
/// sin sufijos).
inline void to_san(const Position& pos, Move move, char* out, eng::usize cap,
                   bool terminal_suffix = true) noexcept {
	eng::usize n = 0u;
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Piece piece = pos.board[from];
	const PieceType type = piece_type(piece);

	if (move_is_castle_king(move)) {
		san_put(out, cap, n, 'O');
		san_put(out, cap, n, '-');
		san_put(out, cap, n, 'O');
	} else if (move_is_castle_queen(move)) {
		san_put(out, cap, n, 'O');
		san_put(out, cap, n, '-');
		san_put(out, cap, n, 'O');
		san_put(out, cap, n, '-');
		san_put(out, cap, n, 'O');
	} else {
		const bool capture = move_is_capture(move);
		if (type == PieceType::Pawn) {
			if (capture) {
				san_put(out, cap, n, static_cast<char>('a' + square_file(from)));
				san_put(out, cap, n, 'x');
			}
		} else {
			san_put(out, cap, n, san_piece_letter(type));

			// Desambiguación: ¿otra pieza del mismo tipo y color puede ir a `to`?
			bool need = false;
			bool same_file = false;
			bool same_rank = false;
			const Color color = piece_color(piece);
			MoveList legal;
			generate_legal(pos, legal);
			for (eng::usize i = 0; i < legal.size(); ++i) {
				const Move other = legal[i];
				if (other == move) {
					continue;
				}
				const Square other_from = move_from(other);
				if (move_to(other) != to) {
					continue;
				}
				const Piece other_piece = pos.board[other_from];
				if (piece_type(other_piece) != type || piece_color(other_piece) != color) {
					continue;
				}
				need = true;
				if (square_file(other_from) == square_file(from)) {
					same_file = true;
				}
				if (square_rank(other_from) == square_rank(from)) {
					same_rank = true;
				}
			}
			if (need) {
				if (!same_file) {
					san_put(out, cap, n, static_cast<char>('a' + square_file(from)));
				} else if (!same_rank) {
					san_put(out, cap, n, static_cast<char>('1' + square_rank(from)));
				} else {
					san_put(out, cap, n, static_cast<char>('a' + square_file(from)));
					san_put(out, cap, n, static_cast<char>('1' + square_rank(from)));
				}
			}
			if (capture) {
				san_put(out, cap, n, 'x');
			}
		}
		san_put_square(out, cap, n, to);
		if (move_promo(move) != PieceType::None) {
			san_put(out, cap, n, '=');
			san_put(out, cap, n, san_piece_letter(move_promo(move)));
		}
	}

	if (terminal_suffix) {
		Position work = pos;
		Undo undo;
		make_move(work, move, undo);
		const Color them = to_move(work);
		if (in_check(work, them)) {
			MoveList replies;
			generate_legal(work, replies);
			san_put(out, cap, n, replies.empty() ? '#' : '+');
		}
	}

	out[n < cap ? n : (cap == 0u ? 0u : cap - 1u)] = '\0';
}

/// Convierte `move` a UCI (`e2e4`, `e7e8q`), útil para depuración y para hablar con
/// herramientas externas.
inline void to_uci(Move move, char* out, eng::usize cap) noexcept {
	eng::usize n = 0u;
	san_put_square(out, cap, n, move_from(move));
	san_put_square(out, cap, n, move_to(move));
	const PieceType promo = move_promo(move);
	if (promo != PieceType::None) {
		san_put(out, cap, n, static_cast<char>(san_piece_letter(promo) - 'A' + 'a'));
	}
	out[n < cap ? n : (cap == 0u ? 0u : cap - 1u)] = '\0';
}

} // namespace eng::board::chess
