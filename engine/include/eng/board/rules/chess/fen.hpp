#pragma once

/// \file fen.hpp
/// Notación **FEN** (Forsyth-Edwards) para ajedrez: leer y escribir posiciones. La
/// usa el libro de aperturas, las tablas de finales, los tests y el comentarista de
/// partidas.
///
/// Interfaz segura: `set_from_fen` recibe un `StringView` y `to_fen` escribe en un
/// `Span<char>` (con truncado comprobado). No hay `char*` ni aritmética de punteros;
/// el parseo avanza por índices sobre la vista.
///
/// Verificación: HOST-140.

#include <eng/board/rules/chess/board.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/text.hpp>

namespace eng::board::chess {

/// Carácter FEN de una pieza (mayúscula blanca, minúscula negra).
[[nodiscard]] constexpr char piece_to_char(Piece piece) noexcept {
	const char letters[7] = {'?', 'P', 'N', 'B', 'R', 'Q', 'K'};
	char c = letters[static_cast<u8>(piece_type(piece))];
	if (piece_color(piece) == Color::Black) {
		c = static_cast<char>(c - 'A' + 'a');
	}
	return c;
}

/// Pieza de un carácter FEN (0 si no es pieza).
[[nodiscard]] constexpr Piece char_to_piece(char c) noexcept {
	const bool lower = c >= 'a' && c <= 'z';
	const char upper = lower ? static_cast<char>(c - 'a' + 'A') : c;
	PieceType type = PieceType::None;
	switch (upper) {
	case 'P': type = PieceType::Pawn; break;
	case 'N': type = PieceType::Knight; break;
	case 'B': type = PieceType::Bishop; break;
	case 'R': type = PieceType::Rook; break;
	case 'Q': type = PieceType::Queen; break;
	case 'K': type = PieceType::King; break;
	default: return kEmptyPiece;
	}
	return make_piece(lower ? Color::Black : Color::White, type);
}

/// Lee una posición FEN desde una vista de texto. `false` si el texto no es válido.
inline bool set_from_fen(Position& pos, eng::util::StringView fen) noexcept {
	pos = Position {};
	eng::usize i = 0u;
	auto skip_spaces = [&]() {
		while (i < fen.size() && fen[i] == ' ') {
			++i;
		}
	};

	board_int rank = 7;
	board_int file = 0;
	while (i < fen.size() && fen[i] != ' ') {
		const char c = fen[i];
		++i;
		if (c == '/') {
			--rank;
			file = 0;
		} else if (c >= '1' && c <= '8') {
			file += static_cast<board_int>(c - '0');
		} else {
			const Piece piece = char_to_piece(c);
			if (piece == kEmptyPiece || rank < 0 || rank > 7 || file < 0 || file > 7) {
				return false;
			}
			pos.board[make_square(static_cast<u8>(file), static_cast<u8>(rank))] = piece;
			++file;
		}
	}

	skip_spaces();
	pos.side = (i < fen.size() && fen[i] == 'b') ? static_cast<u8>(Color::Black)
	                                             : static_cast<u8>(Color::White);
	if (i < fen.size()) {
		++i;
	}

	skip_spaces();
	if (i < fen.size() && fen[i] == '-') {
		pos.castling = 0u;
		++i;
	} else {
		u8 castling = 0u;
		while (i < fen.size() && fen[i] != ' ') {
			switch (fen[i]) {
			case 'K': castling = static_cast<u8>(castling | kCastleWhiteKing); break;
			case 'Q': castling = static_cast<u8>(castling | kCastleWhiteQueen); break;
			case 'k': castling = static_cast<u8>(castling | kCastleBlackKing); break;
			case 'q': castling = static_cast<u8>(castling | kCastleBlackQueen); break;
			default: return false;
			}
			++i;
		}
		pos.castling = castling;
	}

	skip_spaces();
	if (i < fen.size() && fen[i] == '-') {
		pos.ep = kNoSquare;
		++i;
	} else if (i < fen.size()) {
		if (fen[i] < 'a' || fen[i] > 'h' || i + 1u >= fen.size() || fen[i + 1u] < '1' ||
		    fen[i + 1u] > '8') {
			return false;
		}
		pos.ep = make_square(static_cast<u8>(fen[i] - 'a'), static_cast<u8>(fen[i + 1u] - '1'));
		i += 2u;
	}

	skip_spaces();
	u16 halfmove = 0u;
	while (i < fen.size() && eng::util::is_digit(fen[i])) {
		halfmove = static_cast<u16>(halfmove * 10u + static_cast<u16>(fen[i] - '0'));
		++i;
	}
	skip_spaces();
	u16 fullmove = 1u;
	if (i < fen.size() && eng::util::is_digit(fen[i])) {
		fullmove = 0u;
		while (i < fen.size() && eng::util::is_digit(fen[i])) {
			fullmove = static_cast<u16>(fullmove * 10u + static_cast<u16>(fen[i] - '0'));
			++i;
		}
	}
	pos.halfmove = halfmove;
	pos.fullmove = (fullmove == 0u) ? 1u : fullmove;
	rebuild_castle_rooks(pos); // resuelve las torres reales (también Chess960)
	pos.key = compute_key(pos);
	return true;
}

/// Escribe la posición en FEN dentro de `out`. Devuelve la longitud lógica (puede
/// superar `out.size()` si se truncó).
inline eng::usize to_fen(const Position& pos, eng::Span<char> out) noexcept {
	eng::usize n = 0u;
	auto put = [&](char c) {
		if (n < out.size()) {
			out[n] = c;
		}
		++n;
	};
	auto put_text = [&](eng::util::StringView text) {
		for (eng::usize k = 0u; k < text.size(); ++k) {
			put(text[k]);
		}
	};
	auto put_u32 = [&](u32 value) {
		eng::util::StaticString<12> digits;
		(void)eng::util::to_chars_u32(digits, value);
		put_text(digits.view());
	};

	for (board_int rank = 7; rank >= 0; --rank) {
		board_int empty = 0;
		for (board_int file = 0; file < 8; ++file) {
			const Piece piece =
			    pos.board[make_square(static_cast<u8>(file), static_cast<u8>(rank))];
			if (piece == kEmptyPiece) {
				++empty;
				continue;
			}
			if (empty != 0) {
				put(static_cast<char>('0' + empty));
				empty = 0;
			}
			put(piece_to_char(piece));
		}
		if (empty != 0) {
			put(static_cast<char>('0' + empty));
		}
		if (rank > 0) {
			put('/');
		}
	}

	put(' ');
	put(to_move(pos) == Color::Black ? 'b' : 'w');
	put(' ');
	if (pos.castling == 0u) {
		put('-');
	} else {
		if ((pos.castling & kCastleWhiteKing) != 0u) put('K');
		if ((pos.castling & kCastleWhiteQueen) != 0u) put('Q');
		if ((pos.castling & kCastleBlackKing) != 0u) put('k');
		if ((pos.castling & kCastleBlackQueen) != 0u) put('q');
	}
	put(' ');
	if (pos.ep == kNoSquare) {
		put('-');
	} else {
		put(static_cast<char>('a' + square_file(pos.ep)));
		put(static_cast<char>('1' + square_rank(pos.ep)));
	}
	put(' ');
	put_u32(pos.halfmove);
	put(' ');
	put_u32(pos.fullmove);

	if (!out.empty()) {
		const eng::usize nul_at = (n < out.size()) ? n : (out.size() - 1u);
		out[nul_at] = '\0';
	}
	return n;
}

} // namespace eng::board::chess
