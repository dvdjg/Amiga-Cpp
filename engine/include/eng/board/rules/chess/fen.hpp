#pragma once

/// \file fen.hpp
/// Notación **FEN** (Forsyth-Edwards) para ajedrez: leer y escribir posiciones. La
/// usa el libro de aperturas, las tablas de finales, los tests y (más adelante) el
/// comentarista de partidas.
///
/// `set_from_fen` reconstruye el tablero, turno, enroques, al paso y relojes, y
/// recalcula la clave Zobrist. `to_fen` escribe en un buffer del llamador (sin
/// asignación) y trunca de forma segura si no cabe.
///
/// Verificación: HOST-140.

#include <eng/board/rules/chess/board.hpp>

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

/// Escribe `value` en decimal al final del buffer (helper interno de `to_fen`).
inline void write_uint(char* out, eng::usize cap, eng::usize& n, unsigned value) {
	char digits[10];
	eng::usize count = 0u;
	do {
		digits[count++] = static_cast<char>('0' + (value % 10u));
		value /= 10u;
	} while (value != 0u);
	while (count > 0u) {
		if (n + 1u < cap) {
			out[n] = digits[count - 1u];
		}
		++n;
		--count;
	}
}

/// Lee una posición FEN. Devuelve `false` si el texto no es válido.
inline bool set_from_fen(Position& pos, const char* fen) {
	if (fen == nullptr) {
		return false;
	}
	pos = Position {};
	int rank = 7;
	int file = 0;
	const char* cursor = fen;

	while (*cursor != '\0' && *cursor != ' ') {
		const char c = *cursor++;
		if (c == '/') {
			--rank;
			file = 0;
		} else if (c >= '1' && c <= '8') {
			file += (c - '0');
		} else {
			const Piece piece = char_to_piece(c);
			if (piece == kEmptyPiece || rank < 0 || rank > 7 || file < 0 || file > 7) {
				return false;
			}
			pos.board[make_square(static_cast<u8>(file), static_cast<u8>(rank))] = piece;
			++file;
		}
	}

	while (*cursor == ' ') ++cursor;
	pos.side = (*cursor == 'b') ? static_cast<u8>(Color::Black) : static_cast<u8>(Color::White);
	if (*cursor != '\0') ++cursor;

	while (*cursor == ' ') ++cursor;
	if (*cursor == '-') {
		pos.castling = 0u;
		++cursor;
	} else {
		u8 castling = 0u;
		while (*cursor != '\0' && *cursor != ' ') {
			switch (*cursor) {
			case 'K': castling |= kCastleWhiteKing; break;
			case 'Q': castling |= kCastleWhiteQueen; break;
			case 'k': castling |= kCastleBlackKing; break;
			case 'q': castling |= kCastleBlackQueen; break;
			default: return false;
			}
			++cursor;
		}
		pos.castling = castling;
	}

	while (*cursor == ' ') ++cursor;
	if (*cursor == '-') {
		pos.ep = kNoSquare;
		++cursor;
	} else if (*cursor != '\0') {
		if (cursor[0] < 'a' || cursor[0] > 'h' || cursor[1] < '1' || cursor[1] > '8') {
			return false;
		}
		pos.ep = make_square(static_cast<u8>(cursor[0] - 'a'), static_cast<u8>(cursor[1] - '1'));
		cursor += 2;
	}

	while (*cursor == ' ') ++cursor;
	u16 halfmove = 0u;
	while (*cursor >= '0' && *cursor <= '9') {
		halfmove = static_cast<u16>(halfmove * 10u + static_cast<u16>(*cursor - '0'));
		++cursor;
	}
	while (*cursor == ' ') ++cursor;
	u16 fullmove = 1u;
	if (*cursor >= '0' && *cursor <= '9') {
		fullmove = 0u;
		while (*cursor >= '0' && *cursor <= '9') {
			fullmove = static_cast<u16>(fullmove * 10u + static_cast<u16>(*cursor - '0'));
			++cursor;
		}
	}
	pos.halfmove = halfmove;
	pos.fullmove = (fullmove == 0u) ? 1u : fullmove;
	pos.key = compute_key(pos);
	return true;
}

/// Escribe la posición en FEN dentro de `out` (truncando si no cabe).
inline void to_fen(const Position& pos, char* out, eng::usize cap) {
	if (out == nullptr || cap == 0u) {
		return;
	}
	eng::usize n = 0u;
	auto put = [&](char c) {
		if (n + 1u < cap) {
			out[n] = c;
		}
		++n;
	};

	for (int rank = 7; rank >= 0; --rank) {
		int empty = 0;
		for (int file = 0; file < 8; ++file) {
			const Piece piece = pos.board[make_square(static_cast<u8>(file), static_cast<u8>(rank))];
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
	write_uint(out, cap, n, pos.halfmove);
	put(' ');
	write_uint(out, cap, n, pos.fullmove);
	out[n < cap ? n : cap - 1u] = '\0';
}

} // namespace eng::board::chess
