#pragma once

/// \file search.hpp
/// **Buscador adversario genérico** de `eng::board`: negamax con alpha-beta,
/// *iterative deepening*, *quiescence search* y tabla de transposición. Se escribe
/// una sola vez contra el contrato `GameRules` y una policy de evaluación; no
/// conoce ajedrez ni Go.
///
/// Por qué estas piezas (todas clásicas y aptas para 68000):
/// - **Iterative deepening**: busca a profundidad 1, 2, 3… guardando la mejor
///   jugada; se puede **interrumpir** en cualquier nodo (reloj lento) y deja un
///   resultado válido. También ordena la raíz con la mejor jugada anterior.
/// - **Quiescence**: al llegar a la profundidad límite sigue buscando capturas
///   (y todas las jugadas si está en jaque) para no evaluar en medio de un
///   intercambio (efecto horizonte).
/// - **Tabla de transposición**: reutiliza posiciones y sostiene el pondering.
///
/// Presupuesto explícito (`Limits::max_depth`/`max_nodes`) y cancelación con
/// `eng::parallel::StopToken`; si se agota, `Result::aborted` queda a `true` y se
/// conserva la mejor jugada de la última profundidad completada.
///
/// Verificación: HOST-143 (corrección) y HOST-144 (ordenación/TT).

#include <eng/board/core/game.hpp>
#include <eng/board/core/types.hpp>
#include <eng/board/search/tt.hpp>
#include <eng/parallel/parallel.hpp>

namespace eng::board {

template <class Rules, class Eval, class Ordering, u32 TtEntries>
class Searcher {
public:
	using Position = typename Rules::Position;
	using Move = typename Rules::Move;
	using MoveList = typename Rules::MoveList;
	using Undo = typename Rules::Undo;

	static constexpr u32 max_ply = Ordering::max_ply;

	struct Limits {
		u32 max_depth = 6u;
		eng::u64 max_nodes = 0u; ///< 0 = sin límite de nodos
	};

	struct Result {
		Move best_move = kNoMove;
		Score score = kScoreNone;
		u32 depth = 0u;
		eng::u64 nodes = 0u;
		bool aborted = false;
	};

	Searcher() = default;

	/// Vacía la TT y la ordenación (entre partidas).
	void clear() {
		m_tt.clear();
		m_ordering.reset();
		m_previous_best = kNoMove;
	}

	[[nodiscard]] const TranspositionTable<TtEntries>& transposition_table() const { return m_tt; }
	[[nodiscard]] eng::u64 nodes() const { return m_nodes; }

	/// Busca la mejor jugada. Si `stop` se cancela o se agota el presupuesto,
	/// devuelve el resultado de la última profundidad completada.
	Result search(Position& pos, const Limits& limits,
	              const eng::parallel::StopToken& stop = {}) {
		m_nodes = 0u;
		m_aborted = false;
		m_max_nodes = limits.max_nodes;
		m_stop = &stop;
		m_ordering.reset();

		Result result;
		for (u32 depth = 1u; depth <= limits.max_depth; ++depth) {
			const Node out = search_root(pos, depth);
			if (out.aborted) {
				m_aborted = true;
				break;
			}
			result.best_move = out.move;
			result.score = out.score;
			result.depth = depth;
			if (score_is_mate(out.score)) {
				break; // mate encontrado: no hace falta más profundidad
			}
		}
		result.nodes = m_nodes;
		result.aborted = m_aborted;
		return result;
	}

private:
	struct Node {
		Move move = kNoMove;
		Score score = kScoreNone;
		bool aborted = false;
	};

	[[nodiscard]] bool should_abort() noexcept {
		if (m_aborted) {
			return true;
		}
		if (m_stop != nullptr && m_stop->stop_requested()) {
			m_aborted = true;
			return true;
		}
		if (m_max_nodes != 0u && m_nodes >= m_max_nodes) {
			m_aborted = true;
			return true;
		}
		return false;
	}

	Node search_root(Position& pos, u32 depth) {
		Node out;
		MoveList moves;
		Rules::generate_legal(pos, moves);
		m_ordering.order(pos, moves, m_previous_best, 0u);
		Score alpha = kScoreNone;
		Score beta = kScoreInfinite;
		for (eng::usize i = 0u; i < moves.size(); ++i) {
			if (should_abort()) {
				out.aborted = true;
				return out;
			}
			const Move move = moves[i];
			Undo undo;
			Rules::make(pos, move, undo);
			const Score score = static_cast<Score>(-negamax(pos, depth - 1u, -beta, -alpha, 1u));
			Rules::unmake(pos, move, undo);
			if (m_aborted) {
				out.aborted = true;
				return out;
			}
			if (score > out.score) {
				out.score = score;
				out.move = move;
			}
			if (score > alpha) {
				alpha = score;
			}
		}
		m_previous_best = out.move;
		return out;
	}

	Score negamax(Position& pos, u32 depth, Score alpha, Score beta, u32 ply) {
		if (should_abort()) {
			return kScoreNone;
		}
		++m_nodes;
		if (pos.halfmove >= 100u) {
			return 0; // regla de los 50 movimientos
		}
		if (ply >= max_ply) {
			return Eval::evaluate(pos);
		}

		const u32 key = Rules::zobrist(pos);
		Move tt_move = kNoMove;
		Score tt_score = 0;
		u32 tt_depth = 0u;
		TtFlag tt_flag = TtFlag::None;
		if (m_tt.probe(key, tt_move, tt_score, tt_depth, tt_flag)) {
			if (tt_depth >= depth) {
				if (tt_flag == TtFlag::Exact) {
					return tt_score;
				}
				if (tt_flag == TtFlag::Alpha && tt_score <= alpha) {
					return tt_score;
				}
				if (tt_flag == TtFlag::Beta && tt_score >= beta) {
					return tt_score;
				}
			}
		}

		if (depth == 0u) {
			return quiescence(pos, alpha, beta, ply);
		}

		const bool checked = Rules::in_check(pos);
		MoveList moves;
		Rules::generate_legal(pos, moves);
		if (moves.empty()) {
			return checked ? static_cast<Score>(-kScoreMate + static_cast<Score>(ply)) : 0;
		}

		m_ordering.order(pos, moves, tt_move, ply);
		Move best = kNoMove;
		Score best_score = kScoreNone;
		Score a = alpha;
		for (eng::usize i = 0u; i < moves.size(); ++i) {
			if (should_abort()) {
				return kScoreNone;
			}
			const Move move = moves[i];
			Undo undo;
			Rules::make(pos, move, undo);
			const Score score = static_cast<Score>(-negamax(pos, depth - 1u, -beta, -a, ply + 1u));
			Rules::unmake(pos, move, undo);
			if (m_aborted) {
				return kScoreNone;
			}
			if (score > best_score) {
				best_score = score;
				best = move;
			}
			if (best_score > a) {
				a = best_score;
			}
			if (a >= beta) {
				m_ordering.on_beta(pos, move, ply, depth);
				if (!score_is_mate(best_score)) {
					m_tt.store(key, depth, TtFlag::Beta, best_score, move);
				}
				return best_score;
			}
		}
		if (!score_is_mate(best_score)) {
			m_tt.store(key, depth, TtFlag::Exact, best_score, best);
		}
		return best_score;
	}

	Score quiescence(Position& pos, Score alpha, Score beta, u32 ply) {
		if (should_abort()) {
			return alpha;
		}
		++m_nodes;
		if (ply >= max_ply) {
			return Eval::evaluate(pos);
		}
		Score stand = Eval::evaluate(pos);
		if (stand >= beta) {
			return beta;
		}
		if (stand > alpha) {
			alpha = stand;
		}
		const bool checked = Rules::in_check(pos);
		MoveList moves;
		Rules::generate_legal(pos, moves);
		if (moves.empty()) {
			return checked ? static_cast<Score>(-kScoreMate + static_cast<Score>(ply)) : alpha;
		}
		m_ordering.order(pos, moves, kNoMove, ply);
		for (eng::usize i = 0u; i < moves.size(); ++i) {
			const Move move = moves[i];
			if (!checked && !Rules::is_capture(move)) {
				continue; // en quiescence solo interesan las capturas (y evasiones)
			}
			if (should_abort()) {
				return alpha;
			}
			Undo undo;
			Rules::make(pos, move, undo);
			const Score score = static_cast<Score>(-quiescence(pos, -beta, -alpha, ply + 1u));
			Rules::unmake(pos, move, undo);
			if (m_aborted) {
				return alpha;
			}
			if (score > alpha) {
				alpha = score;
			}
			if (alpha >= beta) {
				return alpha;
			}
		}
		return alpha;
	}

	TranspositionTable<TtEntries> m_tt {};
	Ordering m_ordering {};
	eng::u64 m_nodes = 0u;
	eng::u64 m_max_nodes = 0u;
	bool m_aborted = false;
	const eng::parallel::StopToken* m_stop = nullptr;
	Move m_previous_best = kNoMove;
};

} // namespace eng::board
