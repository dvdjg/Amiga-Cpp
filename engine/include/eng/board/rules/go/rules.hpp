#pragma once

/// \file rules.hpp
/// Policy `GoRules` que hace que el Go 9×9 cumpla `eng::board::GameRules`, más la
/// ordenación de jugadas de Go y el alias `GoSearcher`. Así el mismo buscador
/// genérico (`search/search.hpp`) juega al ajedrez y al Go sin cambios.
///
/// Notas:
/// - `in_check` no aplica (devuelve `false`); la quiescence de Go poda por capturas.
/// - `terminal` marca fin de partida cuando no quedan jugadas legales (tablero lleno
///   o sin movimientos): la puntuación la da la evaluación (territorio + capturas).
/// - El **pase** (`kGoPass`) y las dos pasadas consecutivas quedan pendientes.
///
/// Verificación: HOST-152 (reglas) y HOST-153 (evaluación y búsqueda).

#include <eng/board/core/game.hpp>
#include <eng/board/core/types.hpp>
#include <eng/board/eval/go_eval.hpp>
#include <eng/board/rules/go/board.hpp>
#include <eng/board/rules/go/movegen.hpp>
#include <eng/board/search/search.hpp>

namespace eng::board::go {

/// Ordenación de Go: capturas primero, luego puntos centrales.
struct GoOrdering {
	static constexpr eng::u32 max_ply = 64u;

	void reset() noexcept {}

	[[nodiscard]] eng::s32 score(const Position&, Move move) const noexcept {
		if (go_is_pass(move)) {
			return -100000; // el pase se prueba al final
		}
		const u8 point = go_point(move);
		const board_int df = static_cast<board_int>(point_file(point)) - 4;
		const board_int dr = static_cast<board_int>(point_rank(point)) - 4;
		const board_int dist = (df < 0 ? -df : df) + (dr < 0 ? -dr : dr);
		eng::s32 value = static_cast<eng::s32>(8 - dist);
		if ((move & kGoCaptureFlag) != 0u) {
			value += 100000;
		}
		return value;
	}

	void order(const Position& pos, eng::board::MoveList& moves, Move, eng::u32) const noexcept {
		const eng::usize n = moves.size();
		for (eng::usize i = 0u; i < n; ++i) {
			eng::usize best = i;
			eng::s32 best_score = score(pos, moves[i]);
			for (eng::usize j = i + 1u; j < n; ++j) {
				const eng::s32 s = score(pos, moves[j]);
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

	void on_beta(const Position&, Move, eng::u32, eng::u32) noexcept {}
};

} // namespace eng::board::go

namespace eng::board {

/// Reglas de Go 9×9 para el motor genérico de búsqueda.
struct GoRules {
	using Position = go::Position;
	using Move = eng::board::Move;
	using MoveList = eng::board::MoveList;
	using Undo = go::Undo;

	static Position initial() {
		Position position;
		position.to_move = go::kBlack;
		position.key = go::compute_key(position);
		return position;
	}

	static u32 generate_legal(const Position& pos, MoveList& out) {
		return go::generate_legal(pos, out);
	}

	static bool in_check(const Position&) { return false; }

	static bool is_draw(const Position&) { return false; }

	/// Dos pases consecutivos terminan la partida.
	static bool is_over(const Position& pos) { return pos.passes >= 2u; }

	static Score variant_score(const Position&) { return 0; }

	static u32 zobrist(const Position& pos) { return pos.key; }

	static Terminal terminal(const Position& pos) {
		return (pos.passes >= 2u) ? Terminal::GameEnded : Terminal::None;
	}

	static void make(Position& pos, Move move, Undo& undo) { go::make_move(pos, move, undo); }

	static void unmake(Position& pos, Move move, const Undo& undo) { go::unmake_move(pos, move, undo); }

	static bool is_capture(Move move) { return (move & go::kGoCaptureFlag) != 0u; }
};

static_assert(GameRules<GoRules>, "GoRules debe cumplir GameRules");

/// Buscador de Go 9×9 (TT de 256 entradas ≈ 3 kB; el tablero es diminuto).
using GoSearcher = Searcher<GoRules, go::GoEval, go::GoOrdering, 256u, false>;

} // namespace eng::board
