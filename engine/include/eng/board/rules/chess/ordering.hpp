#pragma once

/// \file ordering.hpp
/// Ordenación de jugadas para la búsqueda de ajedrez: **MVV-LVA** (capturar la
/// víctima más valiosa con el atacante más barato), **killer moves** (jugadas que
/// ya provocaron un corte alfa-beta en el mismo ply) e **history heuristic**
/// (frecuencia con que una jugada causó cortes). Ordenar bien es lo que hace que
/// alpha-beta pode mucho: la primera jugada buena multiplica los nodos ahorrados.
///
/// Es una policy de ajedrez para el buscador genérico (`search/search.hpp`): el
/// motor de búsqueda no conoce estas heurísticas, solo pide "ordena esta lista".
///
/// Verificación: HOST-144.

#include <eng/board/core/types.hpp>
#include <eng/board/eval/chess_eval.hpp>
#include <eng/board/rules/chess/board.hpp>
#include <eng/core/arith.hpp>

namespace eng::board::chess {

struct ChessOrdering {
	static constexpr eng::u32 max_ply = 64u;

	/// Hasta dos killers por ply (el segundo es el killer anterior).
	Move killers[max_ply][2] {};
	/// Tabla de historia: [tipo de pieza][casilla destino compacta 0..63].
	eng::u32 history[8][64] {};
	/// Cota superior de la historia (evita desbordar y sesgar para siempre).
	static constexpr eng::u32 history_cap = 1u << 22u;

	void reset() noexcept {
		for (eng::u32 ply = 0u; ply < max_ply; ++ply) {
			killers[ply][0] = kNoMove;
			killers[ply][1] = kNoMove;
		}
		for (int type = 0; type < 8; ++type) {
			for (int sq = 0; sq < 64; ++sq) {
				history[type][sq] >>= 1u;
			}
		}
	}

	/// Puntúa una jugada (mayor = se prueba antes).
	[[nodiscard]] eng::s32 score(const Position& pos, Move move, Move tt_move,
	                             eng::u32 ply) const noexcept {
		if (move == tt_move && !move_none(tt_move)) {
			return 1000000;
		}
		const Piece moving = pos.board[move_from(move)];
		const int type = static_cast<int>(piece_type(moving));
		if (move_is_capture(move)) {
			const Piece victim = move_is_en_passant(move) ? make_piece(opposite(piece_color(moving)),
			                                                          PieceType::Pawn)
			                                              : pos.board[move_to(move)];
			const int victim_value = piece_value(piece_type(victim));
			const int attacker_value = piece_value(piece_type(moving));
			return 500000 + (victim_value << 4) - attacker_value;
		}
		if (ply < max_ply) {
			if (move == killers[ply][0]) {
				return 400000;
			}
			if (move == killers[ply][1]) {
				return 399000;
			}
		}
		if (type >= 0 && type < 8) {
			const eng::u32 h = history[type][compact_square(move_to(move))];
			return static_cast<eng::s32>(h < 300000u ? h : 300000u);
		}
		return 0;
	}

	/// Ordena `moves` de mayor a menor puntuación (selección, sin memoria extra).
	void order(const Position& pos, MoveList& moves, Move tt_move, eng::u32 ply) const noexcept {
		const eng::usize n = moves.size();
		for (eng::usize i = 0u; i < n; ++i) {
			eng::usize best = i;
			eng::s32 best_score = score(pos, moves[i], tt_move, ply);
			for (eng::usize j = i + 1u; j < n; ++j) {
				const eng::s32 s = score(pos, moves[j], tt_move, ply);
				if (s > best_score) {
					best_score = s;
					best = j;
				}
			}
			if (best != i) {
				const Move tmp = moves[i];
				moves[i] = moves[best];
				moves[best] = tmp;
			}
		}
	}

	/// Registra un corte beta: actualiza killers (si es una jugada tranquila) e
	/// historia.
	void on_beta(const Position& pos, Move move, eng::u32 ply, eng::u32 depth) noexcept {
		const Piece moving = pos.board[move_from(move)];
		if (move_is_capture(move)) {
			return;
		}
		if (ply < max_ply) {
			if (killers[ply][0] != move) {
				killers[ply][1] = killers[ply][0];
				killers[ply][0] = move;
			}
		}
		const int type = static_cast<int>(piece_type(moving));
		if (type >= 0 && type < 8) {
			eng::u32& h = history[type][compact_square(move_to(move))];
			// `mulu16` fuerza el `mulu.w` nativo: `depth*depth` en 32 bits sería
			// el libcall `__mulsi3` (~50 ciclos) en el camino caliente.
			h += eng::math::mulu16(static_cast<eng::u16>(depth), static_cast<eng::u16>(depth));
			if (h > history_cap) {
				h = history_cap;
			}
		}
	}
};

} // namespace eng::board::chess
