#pragma once

/// \file board.hpp
/// Reglas de ajedrez sobre tablero **0x88**: estado de la posición, tabla Zobrist,
/// `set_start`, `make`/`unmake` incrementales y detección de ataques/jaque.
///
/// Geometría (0x88): casilla = `rank<<4 | file`, con `file`/`rank` en 0..7. Las
/// casillas fuera del tablero tienen alguno de los bits 7 o 3 activos, así que
/// `(s & 0x88) != 0` las descarta sin comparaciones: es la representación clásica
/// para el 68000, que no tiene bitboards de 64 bits nativos.
///
/// El `key` (Zobrist) se mantiene **incrementalmente** en `make`/`unmake`; el test
/// compara contra `compute_key` para garantizar que no se desincroniza.
///
/// Verificación: HOST-140.

#include <eng/board/core/types.hpp>
#include <eng/board/core/zobrist.hpp>

namespace eng::board::chess {

/// Derechos de enroque (bits).
inline constexpr u8 kCastleWhiteKing = 1u;
inline constexpr u8 kCastleWhiteQueen = 2u;
inline constexpr u8 kCastleBlackKing = 4u;
inline constexpr u8 kCastleBlackQueen = 8u;

/// Casillas clave del enroque (0x88).
inline constexpr Square kA1 = make_square(0u, 0u);
inline constexpr Square kH1 = make_square(7u, 0u);
inline constexpr Square kA8 = make_square(0u, 7u);
inline constexpr Square kH8 = make_square(7u, 7u);

/// Bits del `payload` de una jugada de ajedrez.
inline constexpr u16 kPayloadCapture = 1u << 0u;
inline constexpr u16 kPayloadDoublePush = 1u << 1u;
inline constexpr u16 kPayloadEnPassant = 1u << 2u;
inline constexpr u16 kPayloadCastleKing = 1u << 3u;
inline constexpr u16 kPayloadCastleQueen = 1u << 4u;
inline constexpr u16 kPromoShift = 5u;

[[nodiscard]] constexpr bool move_is_capture(Move move) noexcept {
	return (move_payload(move) & kPayloadCapture) != 0u;
}
[[nodiscard]] constexpr bool move_is_double_push(Move move) noexcept {
	return (move_payload(move) & kPayloadDoublePush) != 0u;
}
[[nodiscard]] constexpr bool move_is_en_passant(Move move) noexcept {
	return (move_payload(move) & kPayloadEnPassant) != 0u;
}
[[nodiscard]] constexpr bool move_is_castle_king(Move move) noexcept {
	return (move_payload(move) & kPayloadCastleKing) != 0u;
}
[[nodiscard]] constexpr bool move_is_castle_queen(Move move) noexcept {
	return (move_payload(move) & kPayloadCastleQueen) != 0u;
}
[[nodiscard]] constexpr PieceType move_promo(Move move) noexcept {
	return static_cast<PieceType>((move_payload(move) >> kPromoShift) & 0x07u);
}

/// Construye una jugada de ajedrez (from/to + flags + pieza de promoción).
[[nodiscard]] constexpr Move chess_move(Square from, Square to, u16 flags = 0u,
                                        PieceType promo = PieceType::None) noexcept {
	return make_move(from, to, static_cast<u16>(flags | (static_cast<u16>(promo) << kPromoShift)));
}

/// Índice 0..11 de pieza para la tabla Zobrist (blancas 0..5, negras 6..11).
[[nodiscard]] constexpr u32 piece_index(Piece piece) noexcept {
	return static_cast<u32>(static_cast<u8>(piece_color(piece)) * 6u +
	                        (static_cast<u8>(piece_type(piece)) - 1u));
}

/// Tabla Zobrist de ajedrez. Construida en `constexpr` (acaba en `.rodata`).
struct ChessZobrist {
	u32 piece[12][64];
	u32 side;
	u32 castling[16];
	u32 ep_file[8];
};

[[nodiscard]] constexpr ChessZobrist make_chess_zobrist() noexcept {
	ChessZobrist table {};
	u32 state = 0x9e3779b9u;
	for (board_int p = 0; p < 12; ++p) {
		for (board_int s = 0; s < 64; ++s) {
			table.piece[p][s] = zobrist_step(state);
		}
	}
	table.side = zobrist_step(state);
	for (board_int c = 0; c < 16; ++c) {
		table.castling[c] = zobrist_step(state);
	}
	for (board_int f = 0; f < 8; ++f) {
		table.ep_file[f] = zobrist_step(state);
	}
	return table;
}

inline constexpr ChessZobrist kZobrist = make_chess_zobrist();

/// Estado completo de una partida (posición + metadatos de reglas).
struct Position {
	u8 board[kBoardSize] {};
	u8 side = static_cast<u8>(Color::White);
	u8 castling = 0u;
	u8 ep = kNoSquare;
	u16 halfmove = 0u;
	u16 fullmove = 1u;
	u32 key = 0u;
};

/// Información necesaria para deshacer una jugada.
struct Undo {
	Piece captured = kEmptyPiece;
	u8 castling = 0u;
	u8 ep = kNoSquare;
	u16 halfmove = 0u;
	u32 key = 0u;
};

/// Pieza que mueve en la posición.
[[nodiscard]] constexpr Color to_move(const Position& pos) noexcept {
	return static_cast<Color>(pos.side);
}

/// Recalcula la clave Zobrist desde cero (referencia de `make`/`unmake`).
[[nodiscard]] inline u32 compute_key(const Position& pos) noexcept {
	u32 key = 0u;
	for (u32 square = 0u; square < kBoardSize; ++square) {
		const Piece piece = pos.board[square];
		if (piece != kEmptyPiece) {
			key ^= kZobrist.piece[piece_index(piece)][compact_square(static_cast<Square>(square))];
		}
	}
	if (to_move(pos) == Color::Black) {
		key ^= kZobrist.side;
	}
	key ^= kZobrist.castling[pos.castling];
	if (pos.ep != kNoSquare) {
		key ^= kZobrist.ep_file[square_file(pos.ep)];
	}
	return key;
}

/// Posición inicial estándar.
inline void set_start(Position& pos) noexcept {
	pos = Position {};
	const PieceType back[8] = {PieceType::Rook,  PieceType::Knight, PieceType::Bishop,
	                           PieceType::Queen, PieceType::King,   PieceType::Bishop,
	                           PieceType::Knight, PieceType::Rook};
	for (u8 file = 0u; file < 8u; ++file) {
		pos.board[make_square(file, 0u)] = make_piece(Color::White, back[file]);
		pos.board[make_square(file, 1u)] = make_piece(Color::White, PieceType::Pawn);
		pos.board[make_square(file, 6u)] = make_piece(Color::Black, PieceType::Pawn);
		pos.board[make_square(file, 7u)] = make_piece(Color::Black, back[file]);
	}
	pos.side = static_cast<u8>(Color::White);
	pos.castling = static_cast<u8>(kCastleWhiteKing | kCastleWhiteQueen | kCastleBlackKing |
	                               kCastleBlackQueen);
	pos.ep = kNoSquare;
	pos.halfmove = 0u;
	pos.fullmove = 1u;
	pos.key = compute_key(pos);
}

/// Casilla del rey del bando (o `kNoSquare` si no está, posición ilegal).
[[nodiscard]] inline Square king_square(const Position& pos, Color color) noexcept {
	const Piece king = make_piece(color, PieceType::King);
	for (u32 square = 0u; square < kBoardSize; ++square) {
		if (pos.board[square] == king) {
			return static_cast<Square>(square);
		}
	}
	return kNoSquare;
}

/// ¿La casilla `square` está atacada por alguna pieza de color `by`?
[[nodiscard]] inline bool is_square_attacked(const Position& pos, Square square, Color by) noexcept {
	const board_int s = static_cast<board_int>(square);

	// Peones: un peón blanco en `s-15`/`s-17` ataca a `s` (y al revés para negras).
	if (by == Color::White) {
		const board_int from1 = s - 15;
		const board_int from2 = s - 17;
		if (from1 >= 0 && from1 <= 127 &&
		    pos.board[from1] == make_piece(Color::White, PieceType::Pawn)) {
			return true;
		}
		if (from2 >= 0 && from2 <= 127 &&
		    pos.board[from2] == make_piece(Color::White, PieceType::Pawn)) {
			return true;
		}
	} else {
		const board_int from1 = s + 15;
		const board_int from2 = s + 17;
		if (from1 >= 0 && from1 <= 127 &&
		    pos.board[from1] == make_piece(Color::Black, PieceType::Pawn)) {
			return true;
		}
		if (from2 >= 0 && from2 <= 127 &&
		    pos.board[from2] == make_piece(Color::Black, PieceType::Pawn)) {
			return true;
		}
	}

	// Caballos.
	constexpr board_int knight_offsets[8] = {31, 33, 14, 18, -31, -33, -14, -18};
	const Piece knight = make_piece(by, PieceType::Knight);
	for (board_int offset : knight_offsets) {
		const board_int target = s + offset;
		if (target >= 0 && target <= 127 && pos.board[target] == knight) {
			return true;
		}
	}

	// Rey.
	constexpr board_int king_offsets[8] = {16, 1, -16, -1, 15, 17, -15, -17};
	const Piece king = make_piece(by, PieceType::King);
	for (board_int offset : king_offsets) {
		const board_int target = s + offset;
		if (target >= 0 && target <= 127 && pos.board[target] == king) {
			return true;
		}
	}

	// Deslizantes diagonales (alfil/dama).
	const Piece bishop = make_piece(by, PieceType::Bishop);
	const Piece queen = make_piece(by, PieceType::Queen);
	constexpr board_int diagonal[4] = {15, 17, -15, -17};
	for (board_int offset : diagonal) {
		board_int target = s + offset;
		while (target >= 0 && target <= 127 && square_valid(static_cast<Square>(target))) {
			const Piece piece = pos.board[target];
			if (piece != kEmptyPiece) {
				if (piece == bishop || piece == queen) {
					return true;
				}
				break;
			}
			target += offset;
		}
	}

	// Deslizantes ortogonales (torre/dama).
	const Piece rook = make_piece(by, PieceType::Rook);
	constexpr board_int orthogonal[4] = {16, 1, -16, -1};
	for (board_int offset : orthogonal) {
		board_int target = s + offset;
		while (target >= 0 && target <= 127 && square_valid(static_cast<Square>(target))) {
			const Piece piece = pos.board[target];
			if (piece != kEmptyPiece) {
				if (piece == rook || piece == queen) {
					return true;
				}
				break;
			}
			target += offset;
		}
	}

	return false;
}

/// ¿El rey del bando `color` está en jaque?
[[nodiscard]] inline bool in_check(const Position& pos, Color color) noexcept {
	const Square king = king_square(pos, color);
	if (king == kNoSquare) {
		return false;
	}
	return is_square_attacked(pos, king, opposite(color));
}

/// Máscara de derechos de enroque que **no** hay que borrar tras la jugada.
[[nodiscard]] constexpr u8 castling_keep_mask(Square from, Square to, Piece moved, Piece captured) noexcept {
	u8 cleared = 0u;
	if (piece_type(moved) == PieceType::King) {
		cleared |= (piece_color(moved) == Color::White)
		               ? static_cast<u8>(kCastleWhiteKing | kCastleWhiteQueen)
		               : static_cast<u8>(kCastleBlackKing | kCastleBlackQueen);
	}
	if (from == kH1) cleared |= kCastleWhiteKing;
	if (from == kA1) cleared |= kCastleWhiteQueen;
	if (from == kH8) cleared |= kCastleBlackKing;
	if (from == kA8) cleared |= kCastleBlackQueen;
	if (captured != kEmptyPiece && piece_type(captured) == PieceType::Rook) {
		if (to == kH1) cleared |= kCastleWhiteKing;
		if (to == kA1) cleared |= kCastleWhiteQueen;
		if (to == kH8) cleared |= kCastleBlackKing;
		if (to == kA8) cleared |= kCastleBlackQueen;
	}
	return static_cast<u8>(~cleared);
}

/// Aplica una jugada pseudo-legal y actualiza Zobrist, enroques, al paso y relojes.
inline void make_move(Position& pos, Move move, Undo& undo) noexcept {
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Piece piece = pos.board[from];
	const PieceType type = piece_type(piece);
	const Color mover = piece_color(piece);

	undo.captured = kEmptyPiece;
	undo.castling = pos.castling;
	undo.ep = pos.ep;
	undo.halfmove = pos.halfmove;
	undo.key = pos.key;

	u32 key = pos.key;
	if (pos.ep != kNoSquare) {
		key ^= kZobrist.ep_file[square_file(pos.ep)];
	}
	key ^= kZobrist.castling[pos.castling];

	Piece captured = kEmptyPiece;
	if (move_is_en_passant(move)) {
		const Square cap = static_cast<Square>((mover == Color::White) ? to - 16u : to + 16u);
		captured = pos.board[cap];
		pos.board[cap] = kEmptyPiece;
		key ^= kZobrist.piece[piece_index(captured)][compact_square(cap)];
	} else if (pos.board[to] != kEmptyPiece) {
		captured = pos.board[to];
		key ^= kZobrist.piece[piece_index(captured)][compact_square(to)];
	}
	undo.captured = captured;

	key ^= kZobrist.piece[piece_index(piece)][compact_square(from)];
	pos.board[from] = kEmptyPiece;

	if (move_promo(move) != PieceType::None) {
		const Piece promoted = make_piece(mover, move_promo(move));
		pos.board[to] = promoted;
		key ^= kZobrist.piece[piece_index(promoted)][compact_square(to)];
	} else {
		pos.board[to] = piece;
		key ^= kZobrist.piece[piece_index(piece)][compact_square(to)];
	}

	if (move_is_castle_king(move)) {
		const u8 rank = square_rank(from);
		const Square rook_from = make_square(7u, rank);
		const Square rook_to = make_square(5u, rank);
		const Piece rook = pos.board[rook_from];
		pos.board[rook_from] = kEmptyPiece;
		pos.board[rook_to] = rook;
		key ^= kZobrist.piece[piece_index(rook)][compact_square(rook_from)];
		key ^= kZobrist.piece[piece_index(rook)][compact_square(rook_to)];
	} else if (move_is_castle_queen(move)) {
		const u8 rank = square_rank(from);
		const Square rook_from = make_square(0u, rank);
		const Square rook_to = make_square(3u, rank);
		const Piece rook = pos.board[rook_from];
		pos.board[rook_from] = kEmptyPiece;
		pos.board[rook_to] = rook;
		key ^= kZobrist.piece[piece_index(rook)][compact_square(rook_from)];
		key ^= kZobrist.piece[piece_index(rook)][compact_square(rook_to)];
	}

	pos.castling = static_cast<u8>(pos.castling & castling_keep_mask(from, to, piece, captured));
	key ^= kZobrist.castling[pos.castling];

	pos.ep = kNoSquare;
	if (type == PieceType::Pawn && move_is_double_push(move)) {
		pos.ep = static_cast<u8>((mover == Color::White) ? from + 16u : from - 16u);
		key ^= kZobrist.ep_file[square_file(pos.ep)];
	}

	if (type == PieceType::Pawn || captured != kEmptyPiece) {
		pos.halfmove = 0u;
	} else {
		++pos.halfmove;
	}

	if (mover == Color::Black) {
		++pos.fullmove;
	}
	pos.side = static_cast<u8>(opposite(mover));
	key ^= kZobrist.side;
	pos.key = key;
}

/// Deshace una jugada aplicada con `make_move` (restaura tablero y clave).
inline void unmake_move(Position& pos, Move move, const Undo& undo) noexcept {
	const Square from = move_from(move);
	const Square to = move_to(move);
	const Color mover = opposite(static_cast<Color>(pos.side));

	Piece piece;
	if (move_promo(move) != PieceType::None) {
		piece = make_piece(mover, PieceType::Pawn);
		pos.board[to] = kEmptyPiece;
	} else {
		piece = pos.board[to];
		pos.board[to] = kEmptyPiece;
	}
	pos.board[from] = piece;

	if (move_is_en_passant(move)) {
		const Square cap = static_cast<Square>((mover == Color::White) ? to - 16u : to + 16u);
		pos.board[cap] = undo.captured;
	} else if (undo.captured != kEmptyPiece) {
		pos.board[to] = undo.captured;
	}

	if (move_is_castle_king(move)) {
		const u8 rank = square_rank(from);
		const Square rook_from = make_square(7u, rank);
		const Square rook_to = make_square(5u, rank);
		pos.board[rook_from] = pos.board[rook_to];
		pos.board[rook_to] = kEmptyPiece;
	} else if (move_is_castle_queen(move)) {
		const u8 rank = square_rank(from);
		const Square rook_from = make_square(0u, rank);
		const Square rook_to = make_square(3u, rank);
		pos.board[rook_from] = pos.board[rook_to];
		pos.board[rook_to] = kEmptyPiece;
	}

	pos.castling = undo.castling;
	pos.ep = undo.ep;
	pos.halfmove = undo.halfmove;
	pos.key = undo.key;
	if (mover == Color::Black) {
		--pos.fullmove;
	}
	pos.side = static_cast<u8>(mover);
}

/// Aplica una **jugada nula** (cambiar el turno sin mover): la usa el null-move
/// pruning. Limpia el al paso y NO toca piezas; el reloj de 50 movimientos avanza
/// (aproximación razonable para la búsqueda).
inline void make_null(Position& pos, Undo& undo) noexcept {
	undo.captured = kEmptyPiece;
	undo.castling = pos.castling;
	undo.ep = pos.ep;
	undo.halfmove = pos.halfmove;
	undo.key = pos.key;

	u32 key = pos.key;
	if (pos.ep != kNoSquare) {
		key ^= kZobrist.ep_file[square_file(pos.ep)];
	}
	pos.ep = kNoSquare;
	pos.side = static_cast<u8>(opposite(to_move(pos)));
	key ^= kZobrist.side;
	pos.key = key;
	++pos.halfmove;
}

/// Deshace una jugada nula.
inline void unmake_null(Position& pos, const Undo& undo) noexcept {
	pos.side = static_cast<u8>(opposite(to_move(pos)));
	pos.castling = undo.castling;
	pos.ep = undo.ep;
	pos.halfmove = undo.halfmove;
	pos.key = undo.key;
}

} // namespace eng::board::chess
