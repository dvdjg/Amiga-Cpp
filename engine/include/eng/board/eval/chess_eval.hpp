#pragma once

/// \file chess_eval.hpp
/// Evaluación estática de ajedrez, ligera y determinista (sin `float`):
/// **material + tablas pieza-casilla (PST) + movilidad + estructura de peones +
/// desarrollo/seguridad del rey**. Es genérica en el sentido del engine: devuelve
/// la puntuación desde la perspectiva del bando al turno (negamax) y trabaja con
/// enteros (centipeones).
///
/// Las PST se calculan con una fórmula de centralización (baratas y simétricas) en
/// vez de tablas de 64 valores: el 68000 agradece no cargar 6 tablas y el resultado
/// es determinista y fácil de razonar. El término de desarrollo reutiliza
/// `extract_development`, la misma fuente que usará el explicador NLG.
///
/// Verificación: HOST-145.

#include <eng/board/core/types.hpp>
#include <eng/board/eval/features.hpp>
#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/movegen.hpp>

namespace eng::board::chess {

/// Valor posicional de cada tipo (centipeones). El rey no cuenta (hay uno por bando).
[[nodiscard]] constexpr int piece_value(PieceType type) noexcept {
	switch (type) {
	case PieceType::Pawn: return 100;
	case PieceType::Knight: return 320;
	case PieceType::Bishop: return 330;
	case PieceType::Rook: return 500;
	case PieceType::Queen: return 900;
	default: return 0;
	}
}

/// Centralización 0..6 (0 = borde, 6 = las cuatro casillas centrales).
[[nodiscard]] constexpr int centrality(u8 file, u8 rank) noexcept {
	const int f = static_cast<int>(file);
	const int r = static_cast<int>(rank);
	const int fdist = (f < 4) ? (3 - f) : (f - 4);
	const int rdist = (r < 4) ? (3 - r) : (r - 4);
	return (3 - fdist) + (3 - rdist);
}

/// Bonus posicional de una pieza en `(file, rank)`, desde la perspectiva del color.
/// Las tablas se espejan por fila (una pieza negra en la fila 7 "vale" como una
/// blanca en la fila 2).
[[nodiscard]] constexpr int pst_bonus(PieceType type, u8 file, u8 rank, Color color,
                                      GamePhase phase) noexcept {
	const int home_rank = (color == Color::White) ? static_cast<int>(rank)
	                                              : (7 - static_cast<int>(rank));
	const int c = centrality(file, rank);
	switch (type) {
	case PieceType::Pawn:
		return ((home_rank - 1) << 3) + (c << 1); // avanzar vale, el centro un poco
	case PieceType::Knight:
		return (c << 3) - ((home_rank == 0) ? 4 : 0); // penaliza quedarse atrás
	case PieceType::Bishop:
		return c << 2;
	case PieceType::Rook:
		return ((home_rank == 6) ? 12 : 0) + c; // séptima fila
	case PieceType::Queen:
		return c;
	case PieceType::King:
		return (phase == GamePhase::Endgame) ? (c << 2) : -((c << 2) + c);
	default:
		return 0;
	}
}

/// Movilidad pseudo-legal de un bando (número de jugadas generadas).
[[nodiscard]] inline int mobility(const Position& pos, Color color) noexcept {
	Position probe = pos;
	probe.side = static_cast<u8>(color);
	MoveList moves;
	generate_pseudo(probe, moves);
	return static_cast<int>(moves.size());
}

/// Estructura de peones desde la perspectiva de las blancas: penaliza doblados y
/// aislados. Devuelve centipeones (positivo = bueno para blancas).
[[nodiscard]] inline Score pawn_structure(const Position& pos) noexcept {
	int counts[2][8] {};
	for (int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece || piece_type(piece) != PieceType::Pawn) {
			continue;
		}
		++counts[static_cast<int>(piece_color(piece))][square_file(static_cast<Square>(raw))];
	}
	int white_penalty = 0;
	int black_penalty = 0;
	for (u8 file = 0u; file < 8u; ++file) {
		if (counts[0][file] > 1) {
			white_penalty += ((counts[0][file] - 1) << 3) + ((counts[0][file] - 1) << 2);
		}
		if (counts[1][file] > 1) {
			black_penalty += ((counts[1][file] - 1) << 3) + ((counts[1][file] - 1) << 2);
		}
		const bool left = (file > 0u) && (counts[0][file - 1u] + counts[1][file - 1u] > 0);
		const bool right = (file < 7u) && (counts[0][file + 1u] + counts[1][file + 1u] > 0);
		if (counts[0][file] > 0 && !left && !right) {
			white_penalty += 15;
		}
		if (counts[1][file] > 0 && !left && !right) {
			black_penalty += 15;
		}
	}
	return static_cast<Score>(black_penalty - white_penalty);
}

/// Desglose de la evaluación desde la perspectiva de las blancas (para depuración
/// y para que el explicador pueda citar componentes concretos).
struct EvalBreakdown {
	Score material = 0;
	Score pst = 0;
	Score mobility = 0;
	Score pawns = 0;
	Score development = 0;
	Score total = 0;
};

/// Evaluación completa desde la perspectiva de las blancas.
[[nodiscard]] inline EvalBreakdown evaluate_white(const Position& pos) noexcept {
	const DevelopmentFeatures features = extract_development(pos);
	EvalBreakdown eval {};

	int material = 0;
	int pst = 0;
	for (int raw = 0; raw < 128; ++raw) {
		if (!square_valid(static_cast<Square>(raw))) {
			continue;
		}
		const Piece piece = pos.board[raw];
		if (piece == kEmptyPiece) {
			continue;
		}
		const PieceType type = piece_type(piece);
		if (type == PieceType::King) {
			continue;
		}
		const Color color = piece_color(piece);
		const int material_value = piece_value(type);
		const int positional = pst_bonus(type, square_file(static_cast<Square>(raw)),
		                                 square_rank(static_cast<Square>(raw)), color,
		                                 features.phase);
		// Suma/resta segun el bando: evita `sign * valor` (sería `__mulsi3` en 68000).
		if (color == Color::White) {
			material += material_value;
			pst += positional;
		} else {
			material -= material_value;
			pst -= positional;
		}
	}
	eval.material = static_cast<Score>(material);
	eval.pst = static_cast<Score>(pst);
	eval.mobility =
	    static_cast<Score>((mobility(pos, Color::White) - mobility(pos, Color::Black)) << 1);
	eval.pawns = pawn_structure(pos);

	// Desarrollo y seguridad del rey: penaliza al bando con piezas sin salir, dama
	// prematura o rey en el centro; premia poder enrocar.
	int development = 0;
	for (int side = 0; side < 2; ++side) {
		const int um = static_cast<int>(features.undeveloped_minors[side]);
		const int uM = static_cast<int>(features.undeveloped_majors[side]);
		int penalty = (um << 3) + (um << 1) + (uM << 2) + (uM << 1);
		if (features.queen_moved_early[side]) {
			penalty += 20;
		}
		if (features.king_in_center[side]) {
			penalty += 25;
		} else if (features.can_castle_king[side] || features.can_castle_queen[side]) {
			penalty -= 10;
		}
		development += (side == 0) ? -penalty : penalty;
	}
	eval.development = static_cast<Score>(development);

	eval.total = static_cast<Score>(eval.material + eval.pst + eval.mobility + eval.pawns +
	                                eval.development);
	return eval;
}

/// Evaluación desde la perspectiva del bando al turno (negamax).
[[nodiscard]] inline Score evaluate(const Position& pos) noexcept {
	const Score white = evaluate_white(pos).total;
	return (to_move(pos) == Color::White) ? white : static_cast<Score>(-white);
}

/// Policy de evaluación para el buscador genérico.
struct ChessEval {
	[[nodiscard]] static Score evaluate(const Position& pos) noexcept {
		return chess::evaluate(pos);
	}
};

} // namespace eng::board::chess
