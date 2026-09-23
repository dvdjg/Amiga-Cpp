#pragma once

/// \file notation.hpp
/// Notación algebraica abreviada **SAN** (y UCI) para ajedrez: convierte una jugada
/// en texto legible. La usan el libro de aperturas, las tablas, los tests y el
/// explicador de partidas.
///
/// Interfaz segura: escribe en un `Span<char>` (con truncado comprobado) y no usa
/// `char*`. Incluye enroque (`O-O`), captura (`x`), promoción (`=Q`),
/// **desambiguación** (columna/fila) y sufijos `+`/`#`.
///
/// Verificación: HOST-142.

#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/movegen.hpp>
#include <eng/core/types/span.hpp>

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

/// Escribe un carácter si cabe; siempre avanza el contador lógico `n`.
inline void san_put(eng::Span<char> out, eng::usize& n, char c) noexcept {
	if (n < out.size()) {
		out[n] = c;
	}
	++n;
}

/// Escribe la casilla como texto (`e4`).
inline void san_put_square(eng::Span<char> out, eng::usize& n, Square square) noexcept {
	san_put(out, n, static_cast<char>('a' + square_file(square)));
	san_put(out, n, static_cast<char>('1' + square_rank(square)));
}

/// Convierte `move` a SAN. Devuelve la longitud lógica (puede superar `out.size()`).
/// `terminal_suffix` añade `+`/`#` (desactívalo para el libro).
inline eng::usize to_san(const Position& pos, Move move, eng::Span<char> out,
                         bool terminal_suffix = true) noexcept {
	eng::usize n = 0u;
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Piece piece = pos.board[from];
	const PieceType type = piece_type(piece);

	if (move_is_castle_king(move)) {
		san_put(out, n, 'O');
		san_put(out, n, '-');
		san_put(out, n, 'O');
	} else if (move_is_castle_queen(move)) {
		san_put(out, n, 'O');
		san_put(out, n, '-');
		san_put(out, n, 'O');
		san_put(out, n, '-');
		san_put(out, n, 'O');
	} else {
		const bool capture = move_is_capture(move);
		if (type == PieceType::Pawn) {
			if (capture) {
				san_put(out, n, static_cast<char>('a' + square_file(from)));
				san_put(out, n, 'x');
			}
		} else {
			san_put(out, n, san_piece_letter(type));

			bool need = false;
			bool same_file = false;
			bool same_rank = false;
			const Color color = piece_color(piece);
			MoveList legal;
			generate_legal(pos, legal);
			for (eng::usize i = 0u; i < legal.size(); ++i) {
				const Move other = legal[i];
				if (other == move || move_to(other) != to) {
					continue;
				}
				const Square other_from = move_from(other);
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
					san_put(out, n, static_cast<char>('a' + square_file(from)));
				} else if (!same_rank) {
					san_put(out, n, static_cast<char>('1' + square_rank(from)));
				} else {
					san_put(out, n, static_cast<char>('a' + square_file(from)));
					san_put(out, n, static_cast<char>('1' + square_rank(from)));
				}
			}
			if (capture) {
				san_put(out, n, 'x');
			}
		}
		san_put_square(out, n, to);
		if (move_promo(move) != PieceType::None) {
			san_put(out, n, '=');
			san_put(out, n, san_piece_letter(move_promo(move)));
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
			san_put(out, n, replies.empty() ? '#' : '+');
		}
	}

	if (!out.empty()) {
		const eng::usize nul_at = (n < out.size()) ? n : (out.size() - 1u);
		out[nul_at] = '\0';
	}
	return n;
}

/// Convierte `move` a UCI (`e2e4`, `e7e8q`).
inline eng::usize to_uci(Move move, eng::Span<char> out) noexcept {
	eng::usize n = 0u;
	san_put_square(out, n, move_from(move));
	san_put_square(out, n, move_to(move));
	const PieceType promo = move_promo(move);
	if (promo != PieceType::None) {
		san_put(out, n, static_cast<char>(san_piece_letter(promo) - 'A' + 'a'));
	}
	if (!out.empty()) {
		const eng::usize nul_at = (n < out.size()) ? n : (out.size() - 1u);
		out[nul_at] = '\0';
	}
	return n;
}

} // namespace eng::board::chess
