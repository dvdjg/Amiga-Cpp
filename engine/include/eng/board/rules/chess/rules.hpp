#pragma once

/// \file rules.hpp
/// Policy `ChessRules` que hace que el ajedrez cumpla `eng::board::GameRules`, más
/// las reglas de fin de partida baratas (50 movimientos, material insuficiente,
/// jaque mate y rey ahogado). La búsqueda adversaria se escribe contra `GameRules`,
/// así que no conoce ajedrez; aquí está el pegamento.
///
/// Verificación: HOST-140.

#include <eng/board/core/game.hpp>
#include <eng/board/core/types.hpp>
#include <eng/board/eval/chess_eval.hpp>
#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/endgame.hpp>
#include <eng/board/rules/chess/fen.hpp>
#include <eng/board/rules/chess/history.hpp>
#include <eng/board/rules/chess/movegen.hpp>
#include <eng/board/rules/chess/ordering.hpp>
#include <eng/board/search/search.hpp>

namespace eng::board::chess {

/// Material insuficiente para dar mate: K vs K, K+pieza menor vs K y K+B vs K+B del
/// mismo color de casilla. Conservador a propósito (no declara tablas dudosas).
[[nodiscard]] inline bool insufficient_material(const Position& pos) noexcept {
	board_int knights = 0;
	board_int bishops = 0;
	board_int light_bishops = 0;
	for (board_int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece) {
			continue;
		}
		switch (piece_type(piece)) {
		case PieceType::Pawn:
		case PieceType::Rook:
		case PieceType::Queen:
			return false;
		case PieceType::Knight:
			++knights;
			break;
		case PieceType::Bishop:
			++bishops;
			if (((square_file(static_cast<Square>(raw)) + square_rank(static_cast<Square>(raw))) & 1u) ==
			    0u) {
				++light_bishops;
			}
			break;
		case PieceType::King:
		case PieceType::None:
			break;
		}
	}
	if (knights == 0 && bishops == 0) {
		return true;
	}
	if (knights + bishops <= 1) {
		return true;
	}
	if (knights == 0 && bishops == 2 && (light_bishops == 0 || light_bishops == 2)) {
		return true;
	}
	return false;
}

/// Estado terminal de una posición (o `None` si la partida sigue).
[[nodiscard]] inline Terminal terminal(const Position& pos) {
	if (pos.halfmove >= 100u) {
		return Terminal::Draw50;
	}
	if (insufficient_material(pos)) {
		return Terminal::InsufficientMaterial;
	}
	Position work = pos;
	MoveList legal;
	generate_legal(work, legal);
	if (legal.empty()) {
		return in_check(pos, to_move(pos)) ? Terminal::Checkmate : Terminal::Stalemate;
	}
	return Terminal::None;
}

/// Estado terminal considerando el **historial** de la partida (repetición de tres
/// posiciones). `history` debe contener la clave de la posición actual.
[[nodiscard]] inline Terminal terminal(const Position& pos, const PositionHistory<512>& history) {
	const Terminal base = terminal(pos);
	if (base != Terminal::None) {
		return base;
	}
	if (history.repetitions(pos.key, static_cast<eng::u32>(pos.halfmove) + 1u) >= 3u) {
		return Terminal::Repetition;
	}
	return Terminal::None;
}

} // namespace eng::board::chess

namespace eng::board {

/// Reglas de ajedrez para el motor genérico de búsqueda.
struct ChessRules {
	using Position = chess::Position;
	using Move = eng::board::Move;
	using MoveList = eng::board::MoveList;
	using Undo = chess::Undo;

	static Position initial() {
		Position position;
		chess::set_start(position);
		return position;
	}

	static u32 generate_legal(const Position& pos, MoveList& out) {
		return chess::generate_legal(pos, out);
	}

	static bool in_check(const Position& pos) { return chess::in_check(pos, chess::to_move(pos)); }

	static u32 zobrist(const Position& pos) { return pos.key; }

	static Terminal terminal(const Position& pos) { return chess::terminal(pos); }

	static void make(Position& pos, Move move, Undo& undo) { chess::make_move(pos, move, undo); }

	static void unmake(Position& pos, Move move, const Undo& undo) { chess::unmake_move(pos, move, undo); }

	static bool is_capture(Move move) { return chess::move_is_capture(move); }

	static void make_null(Position& pos, Undo& undo) { chess::make_null(pos, undo); }

	static void unmake_null(Position& pos, const Undo& undo) { chess::unmake_null(pos, undo); }

	/// Aproximación de final: sin damas y con poco material no peón. El null-move
	/// se desactiva aquí para evitar zugzwang.
	static bool is_endgame(const Position& pos) {
		const chess::MaterialCount material = chess::count_material(pos);
		if (material.queens[0] + material.queens[1] != 0u) {
			return false;
		}
		const board_int non_pawn = static_cast<board_int>(material.knights[0] + material.knights[1] +
		                                      material.bishops[0] + material.bishops[1] +
		                                      material.rooks[0] + material.rooks[1]);
		return non_pawn <= 4;
	}
};

static_assert(GameRules<ChessRules>, "ChessRules debe cumplir GameRules");

/// Buscador de ajedrez listo para usar: negamax/alpha-beta + quiescence + TT de
/// 1024 entradas (≈ 12 kB) y las heurísticas de ordenación de ajedrez.
using ChessSearcher = Searcher<ChessRules, chess::ChessEval, chess::ChessOrdering, 1024u, false>;

/// Variante con **null-move pruning** (perfiles con presupuesto, p. ej. `P256+`).
using ChessSearcherNull = Searcher<ChessRules, chess::ChessEval, chess::ChessOrdering, 1024u, true>;

} // namespace eng::board
