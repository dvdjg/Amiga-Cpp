#pragma once

/// \file features.hpp
/// Extracción de **rasgos de desarrollo** de una posición de ajedrez, en una
/// estructura pequeña (≤ 16 B). La usan a la vez la evaluación (términos de
/// desarrollo y seguridad del rey) y el explicador en lenguaje natural: una sola
/// fuente de verdad, como exige el diseño.
///
/// Rasgos: piezas menores/mayores sin desarrollar, dama movida prematuramente, rey
/// en el centro, derechos de enroque, torres en columnas abiertas y fase de partida.
///
/// Verificación: HOST-145.

#include <eng/board/core/types.hpp>
#include <eng/board/rules/chess/board.hpp>

namespace eng::board::chess {

/// Fase aproximada de la partida (0 = apertura, 3 = final).
enum class GamePhase : u8 {
	Opening = 0u,
	EarlyMiddlegame = 1u,
	Middlegame = 2u,
	Endgame = 3u,
};

/// Rasgos de desarrollo por bando. Índice 0 = blancas, 1 = negras.
struct DevelopmentFeatures {
	u8 undeveloped_minors[2] {0u, 0u}; ///< caballos + alfiles en su casilla inicial
	u8 undeveloped_majors[2] {0u, 0u}; ///< torres + dama sin mover
	bool queen_moved_early[2] {false, false};
	bool king_in_center[2] {false, false};
	bool can_castle_king[2] {false, false};
	bool can_castle_queen[2] {false, false};
	u8 rooks_on_open_files[2] {0u, 0u};
	GamePhase phase = GamePhase::Opening;
};

/// ¿La columna `file` tiene algún peón de cualquier color?
[[nodiscard]] inline bool file_has_pawn(const Position& pos, u8 file) noexcept {
	for (u8 rank = 0u; rank < 8u; ++rank) {
		const Piece piece = pos.board[make_square(file, rank)];
		if (piece != kEmptyPiece && piece_type(piece) == PieceType::Pawn) {
			return true;
		}
	}
	return false;
}

/// Extrae los rasgos de desarrollo de `pos`.
[[nodiscard]] inline DevelopmentFeatures extract_development(const Position& pos) noexcept {
	DevelopmentFeatures f {};

	// Piezas en su casilla inicial (blancas en la fila 1, negras en la 8).
	struct HomeSquare {
		Square square;
		Piece piece;
	};
	const HomeSquare white_home[8] = {
	    {make_square(0u, 0u), make_piece(Color::White, PieceType::Rook)},
	    {make_square(1u, 0u), make_piece(Color::White, PieceType::Knight)},
	    {make_square(2u, 0u), make_piece(Color::White, PieceType::Bishop)},
	    {make_square(3u, 0u), make_piece(Color::White, PieceType::Queen)},
	    {make_square(5u, 0u), make_piece(Color::White, PieceType::Bishop)},
	    {make_square(6u, 0u), make_piece(Color::White, PieceType::Knight)},
	    {make_square(7u, 0u), make_piece(Color::White, PieceType::Rook)},
	    {make_square(4u, 0u), make_piece(Color::White, PieceType::King)},
	};
	const HomeSquare black_home[8] = {
	    {make_square(0u, 7u), make_piece(Color::Black, PieceType::Rook)},
	    {make_square(1u, 7u), make_piece(Color::Black, PieceType::Knight)},
	    {make_square(2u, 7u), make_piece(Color::Black, PieceType::Bishop)},
	    {make_square(3u, 7u), make_piece(Color::Black, PieceType::Queen)},
	    {make_square(5u, 7u), make_piece(Color::Black, PieceType::Bishop)},
	    {make_square(6u, 7u), make_piece(Color::Black, PieceType::Knight)},
	    {make_square(7u, 7u), make_piece(Color::Black, PieceType::Rook)},
	    {make_square(4u, 7u), make_piece(Color::Black, PieceType::King)},
	};

	for (int side = 0; side < 2; ++side) {
		const HomeSquare* home = (side == 0) ? white_home : black_home;
		for (int i = 0; i < 8; ++i) {
			if (pos.board[home[i].square] != home[i].piece) {
				continue;
			}
			const PieceType type = piece_type(home[i].piece);
			if (type == PieceType::Knight || type == PieceType::Bishop) {
				++f.undeveloped_minors[side];
			} else if (type == PieceType::Rook || type == PieceType::Queen) {
				++f.undeveloped_majors[side];
			}
			// Peón y rey no cuentan como "desarrollo".
		}
	}

	const u8 castle_king[2] = {kCastleWhiteKing, kCastleBlackKing};
	const u8 castle_queen[2] = {kCastleWhiteQueen, kCastleBlackQueen};
	for (int side = 0; side < 2; ++side) {
		f.can_castle_king[side] = (pos.castling & castle_king[side]) != 0u;
		f.can_castle_queen[side] = (pos.castling & castle_queen[side]) != 0u;
	}

	// Fase: número total de piezas no peón y no rey.
	int non_pawns = 0;
	for (int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece) {
			continue;
		}
		const PieceType type = piece_type(piece);
		if (type != PieceType::Pawn && type != PieceType::King) {
			++non_pawns;
		}
	}
	if (non_pawns >= 12) {
		f.phase = GamePhase::Opening;
	} else if (non_pawns >= 8) {
		f.phase = GamePhase::EarlyMiddlegame;
	} else if (non_pawns >= 4) {
		f.phase = GamePhase::Middlegame;
	} else {
		f.phase = GamePhase::Endgame;
	}

	// Dama prematura: fuera de d1/d8, antes de la jugada 10 y con menores sin salir.
	const Square queen_home[2] = {make_square(3u, 0u), make_square(3u, 7u)};
	for (int side = 0; side < 2; ++side) {
		const Piece queen = make_piece(static_cast<Color>(side), PieceType::Queen);
		bool queen_on_home = false;
		for (int raw = 0; raw < 128; ++raw) {
			if (square_valid(static_cast<Square>(raw)) && pos.board[raw] == queen &&
			    static_cast<Square>(raw) == queen_home[side]) {
				queen_on_home = true;
				break;
			}
		}
		f.queen_moved_early[side] = !queen_on_home && pos.fullmove <= 10u &&
		                            f.undeveloped_minors[side] >= 2u;
	}

	// Rey en el centro: sigue en la columna e y no se ha enrocado en medio juego.
	const Square king_home[2] = {make_square(4u, 0u), make_square(4u, 7u)};
	for (int side = 0; side < 2; ++side) {
		const Square king = king_square(pos, static_cast<Color>(side));
		f.king_in_center[side] =
		    (king == king_home[side]) && (pos.fullmove >= 10u) &&
		    (f.phase == GamePhase::EarlyMiddlegame || f.phase == GamePhase::Middlegame);
	}

	// Torres en columnas abiertas (sin peones propios ni rivales).
	for (int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece || piece_type(piece) != PieceType::Rook) {
			continue;
		}
		const int side = static_cast<int>(piece_color(piece));
		if (!file_has_pawn(pos, square_file(static_cast<Square>(raw)))) {
			++f.rooks_on_open_files[side];
		}
	}

	return f;
}

} // namespace eng::board::chess
