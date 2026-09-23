#pragma once

/// \file ordering.hpp
/// Ordenación de jugadas para la búsqueda de ajedrez: **MVV-LVA** (capturar la
/// víctima más valiosa con el atacante más barato), **killer moves** (jugadas que
/// ya provocaron un corte alfa-beta en el mismo ply) e **history heuristic**.
/// Ordenar bien es lo que hace que alpha-beta pode mucho.
///
/// Es una policy de ajedrez para el buscador genérico (`search/search.hpp`): el
/// motor de búsqueda no conoce estas heurísticas, solo pide "ordena esta lista".
///
/// Las tablas internas usan `eng::util::Array` (sin arrays C desnudos). La
/// puntuación de ordenación es `s32` porque codifica bases de 10^5–10^6 y **excede
/// 16 bits**: aquí el ancho de 32 es imprescindible, no un descuido.
///
/// Verificación: HOST-144.

#include <eng/board/core/types.hpp>
#include <eng/board/eval/chess_eval.hpp>
#include <eng/board/rules/chess/board.hpp>
#include <eng/core/math/arith.hpp>
#include <eng/core/util/array.hpp>

namespace eng::board::chess {

struct ChessOrdering {
	static constexpr eng::u32 max_ply = 64u;
	static constexpr eng::u32 history_cap = 1u << 22u;

	/// Hasta dos killers por ply (el segundo es el killer anterior).
	eng::util::Array<eng::util::Array<Move, 2u>, max_ply> killers {};
	/// Tabla de historia: [tipo de pieza][casilla destino compacta 0..63].
	eng::util::Array<eng::util::Array<eng::u32, 64u>, 8u> history {};

	void reset() noexcept {
		for (eng::u32 ply = 0u; ply < max_ply; ++ply) {
			killers[ply][0] = kNoMove;
			killers[ply][1] = kNoMove;
		}
		for (eng::u32 type = 0u; type < 8u; ++type) {
			for (eng::u32 square = 0u; square < 64u; ++square) {
				history[type][square] >>= 1u;
			}
		}
	}

	/// Puntúa una jugada (mayor = se prueba antes). `s32` imprescindible: las bases
	/// (TT, captura, killer) llegan a 10^6.
	[[nodiscard]] eng::s32 score(const Position& pos, Move move, Move tt_move,
	                             eng::u32 ply) const noexcept {
		if (move == tt_move && !move_none(tt_move)) {
			return 1000000;
		}
		const Piece moving = pos.board[move_from(move)];
		const board_int type = static_cast<board_int>(piece_type(moving));
		if (move_is_capture(move)) {
			const Piece victim =
			    move_is_en_passant(move)
			        ? make_piece(opposite(piece_color(moving)), PieceType::Pawn)
			        : pos.board[move_to(move)];
			const eng::s32 victim_value = static_cast<eng::s32>(piece_value(piece_type(victim)));
			const eng::s32 attacker_value =
			    static_cast<eng::s32>(piece_value(piece_type(moving)));
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
			const eng::u32 h = history[static_cast<eng::u32>(type)]
			                          [compact_square(move_to(move))];
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

	/// Registra un corte beta: actualiza killers (si es jugada tranquila) e historia.
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
		const board_int type = static_cast<board_int>(piece_type(moving));
		if (type >= 0 && type < 8) {
			eng::u32& h = history[static_cast<eng::u32>(type)][compact_square(move_to(move))];
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
