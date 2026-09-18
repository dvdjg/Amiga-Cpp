#pragma once

/// \file movegen.hpp
/// Generación de jugadas de ajedrez y `perft` (conteo de nodos) para validar la
/// legalidad. Primero se generan las jugadas **pseudo-legales** y después se filtran
/// las que dejan al rey propio en jaque aplicando/deshaciendo cada una.
///
/// El filtrado con `make`/`unmake` evita reimplementar la detección de clavadas,
/// jaques descubiertos, enroque a través de casillas atacadas y al paso: la propia
/// posición decide. `perft` compara el conteo con los valores conocidos (posición
/// inicial, Kiwipete, finales con al paso/promoción).
///
/// Verificación: HOST-140.

#include <eng/board/core/types.hpp>
#include <eng/board/rules/chess/board.hpp>

namespace eng::board::chess {

/// Añade las cuatro promociones (dama, caballo, torre, alfil) de `from` a `to`.
inline void add_promotions(Square from, Square to, u16 flags, MoveList& out) {
	const PieceType promotions[4] = {PieceType::Queen, PieceType::Knight, PieceType::Rook,
	                                 PieceType::Bishop};
	for (PieceType promo : promotions) {
		out.push_back(chess_move(from, to, flags, promo));
	}
}

/// Saltos (caballo/rey) desde `from`.
template <eng::usize N>
inline void add_step_moves(const Position& pos, Square from, Color us, const board_int (&offsets)[N],
                           MoveList& out) {
	const board_int s = static_cast<board_int>(from);
	for (eng::usize i = 0; i < N; ++i) {
		const board_int target = s + offsets[i];
		if (target < 0 || target > 127 || !square_valid(static_cast<Square>(target))) {
			continue;
		}
		const Piece target_piece = pos.board[target];
		if (target_piece != kEmptyPiece && piece_color(target_piece) == us) {
			continue;
		}
		const u16 flags = (target_piece != kEmptyPiece) ? kPayloadCapture : 0u;
		out.push_back(chess_move(from, static_cast<Square>(target), flags));
	}
}

/// Deslizantes (alfil/torre/dama) desde `from`.
template <eng::usize N>
inline void add_slider_moves(const Position& pos, Square from, Color us, const board_int (&offsets)[N],
                             MoveList& out) {
	const board_int s = static_cast<board_int>(from);
	for (eng::usize i = 0; i < N; ++i) {
		const board_int offset = offsets[i];
		board_int target = s + offset;
		while (target >= 0 && target <= 127 && square_valid(static_cast<Square>(target))) {
			const Piece target_piece = pos.board[target];
			if (target_piece == kEmptyPiece) {
				out.push_back(chess_move(from, static_cast<Square>(target), 0u));
			} else {
				if (piece_color(target_piece) != us) {
					out.push_back(
					    chess_move(from, static_cast<Square>(target), kPayloadCapture));
				}
				break;
			}
			target += offset;
		}
	}
}

/// Enroques legales (derechos + camino libre + casillas no atacadas).
inline void add_castling(const Position& pos, Square from, Color us, MoveList& out) {
	const Square home = (us == Color::White) ? make_square(4u, 0u) : make_square(4u, 7u);
	if (from != home || in_check(pos, us)) {
		return;
	}
	const Color them = opposite(us);
	const u8 rank = square_rank(home);
	const u8 king_side = (us == Color::White) ? kCastleWhiteKing : kCastleBlackKing;
	const u8 queen_side = (us == Color::White) ? kCastleWhiteQueen : kCastleBlackQueen;

	if ((pos.castling & king_side) != 0u) {
		const Square f = make_square(5u, rank);
		const Square g = make_square(6u, rank);
		if (pos.board[f] == kEmptyPiece && pos.board[g] == kEmptyPiece &&
		    !is_square_attacked(pos, f, them) && !is_square_attacked(pos, g, them)) {
			out.push_back(chess_move(home, g, kPayloadCastleKing));
		}
	}
	if ((pos.castling & queen_side) != 0u) {
		const Square b = make_square(1u, rank);
		const Square c = make_square(2u, rank);
		const Square d = make_square(3u, rank);
		if (pos.board[b] == kEmptyPiece && pos.board[c] == kEmptyPiece &&
		    pos.board[d] == kEmptyPiece && !is_square_attacked(pos, d, them) &&
		    !is_square_attacked(pos, c, them)) {
			out.push_back(chess_move(home, c, kPayloadCastleQueen));
		}
	}
}

/// Genera todas las jugadas pseudo-legales del bando al turno.
inline void generate_pseudo(const Position& pos, MoveList& out) {
	out.clear();
	const Color us = to_move(pos);
	static constexpr board_int knight_offsets[8] = {31, 33, 14, 18, -31, -33, -14, -18};
	static constexpr board_int king_offsets[8] = {16, 1, -16, -1, 15, 17, -15, -17};
	static constexpr board_int diagonal[4] = {15, 17, -15, -17};
	static constexpr board_int orthogonal[4] = {16, 1, -16, -1};

	for (board_int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Square from = static_cast<Square>(raw);
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece || piece_color(piece) != us) {
			continue;
		}
		switch (piece_type(piece)) {
		case PieceType::Pawn: {
			const board_int dir = (us == Color::White) ? 16 : -16;
			const board_int start_rank = (us == Color::White) ? 1 : 6;
			const board_int promo_rank = (us == Color::White) ? 7 : 0;
			const board_int one = raw + dir;
			if (one >= 0 && one <= 127 && square_valid(static_cast<Square>(one)) &&
			    pos.board[one] == kEmptyPiece) {
				if (square_rank(static_cast<Square>(one)) == promo_rank) {
					add_promotions(from, static_cast<Square>(one), 0u, out);
				} else {
					out.push_back(chess_move(from, static_cast<Square>(one), 0u));
					const board_int two = raw + 2 * dir;
					if (square_rank(from) == start_rank && two >= 0 && two <= 127 &&
					    pos.board[two] == kEmptyPiece) {
						out.push_back(
						    chess_move(from, static_cast<Square>(two), kPayloadDoublePush));
					}
				}
			}
			for (board_int dc = -1; dc <= 1; dc += 2) {
				const board_int target = raw + dir + dc;
				if (target < 0 || target > 127 ||
				    !square_valid(static_cast<Square>(target))) {
					continue;
				}
				const Piece target_piece = pos.board[target];
				if (target_piece != kEmptyPiece && piece_color(target_piece) != us) {
					if (square_rank(static_cast<Square>(target)) == promo_rank) {
						add_promotions(from, static_cast<Square>(target), kPayloadCapture, out);
					} else {
						out.push_back(chess_move(from, static_cast<Square>(target),
						                          kPayloadCapture));
					}
				} else if (target_piece == kEmptyPiece && pos.ep != kNoSquare &&
				           static_cast<Square>(target) == pos.ep) {
					out.push_back(chess_move(from, static_cast<Square>(target),
					                          static_cast<u16>(kPayloadCapture |
					                                           kPayloadEnPassant)));
				}
			}
			break;
		}
		case PieceType::Knight:
			add_step_moves(pos, from, us, knight_offsets, out);
			break;
		case PieceType::Bishop:
			add_slider_moves(pos, from, us, diagonal, out);
			break;
		case PieceType::Rook:
			add_slider_moves(pos, from, us, orthogonal, out);
			break;
		case PieceType::Queen:
			add_slider_moves(pos, from, us, diagonal, out);
			add_slider_moves(pos, from, us, orthogonal, out);
			break;
		case PieceType::King:
			add_step_moves(pos, from, us, king_offsets, out);
			add_castling(pos, from, us, out);
			break;
		case PieceType::None:
			break;
		}
	}
}

/// Genera las jugadas **legales**: filtra las pseudo-legales que dejan al rey propio
/// en jaque (o a través de una casilla atacada en el enroque).
inline u32 generate_legal(const Position& pos, MoveList& out) {
	out.clear();
	MoveList pseudo;
	generate_pseudo(pos, pseudo);
	const Color us = to_move(pos);
	const Color them = opposite(us);
	Position work = pos;
	const eng::usize count = pseudo.size();
	for (eng::usize i = 0; i < count; ++i) {
		const Move move = pseudo[i];
		Undo undo;
		make_move(work, move, undo);
		if (!is_square_attacked(work, king_square(work, us), them)) {
			out.push_back(move);
		}
		unmake_move(work, move, undo);
	}
	return static_cast<u32>(out.size());
}

/// Conteo de nodos de un árbol de profundidad `depth` (perft).
inline eng::u64 perft(Position& pos, u32 depth) {
	if (depth == 0u) {
		return 1u;
	}
	MoveList legal;
	generate_legal(pos, legal);
	if (depth == 1u) {
		return static_cast<eng::u64>(legal.size());
	}
	eng::u64 nodes = 0u;
	const eng::usize count = legal.size();
	for (eng::usize i = 0; i < count; ++i) {
		const Move move = legal[i];
		Undo undo;
		make_move(pos, move, undo);
		nodes += perft(pos, depth - 1u);
		unmake_move(pos, move, undo);
	}
	return nodes;
}

} // namespace eng::board::chess
