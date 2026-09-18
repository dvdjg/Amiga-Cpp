#pragma once

/// \file search.hpp
/// **Buscador adversario genérico** de `eng::board`: negamax con alpha-beta,
/// *iterative deepening*, *quiescence search*, tabla de transposición, **PV**
/// (línea principal), **Multi-PV** (varias mejores líneas) y, opcionalmente,
/// **null-move pruning**. Se escribe una sola vez contra el contrato `GameRules` y
/// una policy de evaluación; no conoce ajedrez ni Go.
///
/// Interfaz segura: los buffers de salida son `Span` (`analyze_move`,
/// `search_multi_pv`) y las tablas internas usan `eng::util::Array`, sin punteros ni
/// aritmética de punteros. La cancelación es un `StopToken`.
///
/// Por qué estas piezas (todas clásicas y aptas para 68000):
/// - **Iterative deepening**: busca a profundidad 1, 2, 3… guardando la mejor
///   jugada; se puede **interrumpir** en cualquier nodo y deja un resultado válido.
/// - **Quiescence**: al llegar a la profundidad límite sigue buscando capturas (y
///   todas las jugadas si está en jaque) para no evaluar en medio de un intercambio.
/// - **Tabla de transposición**: reutiliza posiciones y sostiene el pondering.
/// - **PV/Multi-PV**: devuelve la línea principal y las N mejores candidatas.
/// - **Null-move** (`EnableNullMove`): poda ramas tranquilas con presupuesto.
///
/// Verificación: HOST-143, HOST-144 y HOST-148.

#include <eng/board/core/game.hpp>
#include <eng/board/core/types.hpp>
#include <eng/board/search/pruning.hpp>
#include <eng/board/search/tt.hpp>
#include <eng/core/span.hpp>
#include <eng/core/util/array.hpp>
#include <eng/parallel/parallel.hpp>

namespace eng::board {

template <class Rules, class Eval, class Ordering, u32 TtEntries, bool EnableNullMove = false>
class Searcher {
public:
	using Position = typename Rules::Position;
	using Move = typename Rules::Move;
	using MoveList = typename Rules::MoveList;
	using Undo = typename Rules::Undo;

	static constexpr u32 max_ply = Ordering::max_ply;
	static constexpr u32 pv_max = 24u;
	static constexpr u32 root_move_max = 256u;

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

	/// Una línea del análisis: jugada, puntuación y línea principal.
	struct Line {
		Move move = kNoMove;
		Score score = kScoreNone;
		u32 length = 0u;
		eng::util::Array<Move, pv_max> pv {};
	};

	Searcher() = default;

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
				break;
			}
		}
		result.nodes = m_nodes;
		result.aborted = m_aborted;
		return result;
	}

	/// **Pondering**: igual que `search`, pensado para lanzarse con un `stop` que el
	/// juego cancela cuando el rival mueve. La TT y la ordenación sobreviven entre
	/// llamadas, así que si el rival juega la jugada prevista se reaprovecha el árbol.
	Result ponder(Position& pos, const Limits& limits, const eng::parallel::StopToken& stop) {
		return search(pos, limits, stop);
	}

	/// Puntúa una jugada de la raíz con **ventana completa** y rellena su PV.
	Score analyze_move(Position& pos, Move move, u32 depth, eng::Span<Move> pv_out,
	                   u32& pv_len) {
		m_aborted = false;
		m_max_nodes = 0u;
		m_stop = nullptr;
		pv_len = 0u;
		Undo undo;
		Rules::make(pos, move, undo);
		m_pv_len[1] = 0u;
		const Score score =
		    (depth == 0u)
		        ? Eval::evaluate(pos)
		        : static_cast<Score>(
		              -negamax(pos, depth - 1u, -kScoreInfinite, kScoreInfinite, 1u));
		Rules::unmake(pos, move, undo);
		if (m_aborted) {
			return kScoreNone;
		}
		if (pv_out.size() > 0u) {
			u32 out = 1u;
			pv_out[0] = move;
			const u32 child = (depth >= 1u) ? m_pv_len[1] : 0u;
			for (u32 i = 0u; i < child && out < pv_out.size(); ++i) {
				pv_out[out] = m_pv_table[1][i];
				++out;
			}
			pv_len = out;
		}
		return score;
	}

	/// **Multi-PV**: las `wanted` mejores jugadas con su puntuación y PV. Reparte la
	/// puntuación exacta de cada jugada de la raíz con `eng::parallel::for_each_index`
	/// (secuencial en Amiga, hilos en host); el ranking es estable, así que el
	/// resultado es determinista. Devuelve cuántas líneas escribió.
	u32 search_multi_pv(Position& pos, const Limits& limits, u32 wanted, eng::Span<Line> out,
	                    u32 threads = 0u) {
		if (wanted == 0u || out.size() == 0u) {
			return 0u;
		}
		if (wanted > out.size()) {
			wanted = static_cast<u32>(out.size());
		}
		MoveList root_moves;
		Rules::generate_legal(pos, root_moves);
		m_ordering.order(pos, root_moves, m_previous_best, 0u);
		u32 n = static_cast<u32>(root_moves.size());
		if (n == 0u) {
			return 0u;
		}
		if (n > root_move_max) {
			n = root_move_max;
		}
		if (threads == 0u) {
			threads = eng::parallel::hardware_threads();
		}

		eng::util::Array<Score, root_move_max> scores {};
		for (u32 i = 0u; i < n; ++i) {
			scores[i] = kScoreNone;
		}

		struct Job {
			const Position* pos;
			const MoveList* moves;
			Score* scores;
			u32 depth;
		};
		Job job {&pos, &root_moves, scores.elems, limits.max_depth};
		auto task = [&job](u32 index) {
			Position work = *job.pos;
			Searcher<Rules, Eval, Ordering, 256u, EnableNullMove> local;
			u32 length = 0u;
			job.scores[index] =
			    local.analyze_move(work, (*job.moves)[index], job.depth, {}, length);
		};
		eng::parallel::for_each_index(n, threads, task);

		eng::util::Array<u32, root_move_max> order_index {};
		for (u32 i = 0u; i < n; ++i) {
			order_index[i] = i;
		}
		for (u32 i = 0u; i < n; ++i) {
			u32 best = i;
			for (u32 j = i + 1u; j < n; ++j) {
				if (scores[order_index[j]] > scores[order_index[best]]) {
					best = j;
				}
			}
			const u32 tmp = order_index[i];
			order_index[i] = order_index[best];
			order_index[best] = tmp;
		}

		u32 produced = 0u;
		for (u32 r = 0u; r < wanted; ++r) {
			const u32 index = order_index[r];
			Line& line = out[produced];
			line.move = root_moves[index];
			line.score = scores[index];
			u32 length = 0u;
			line.score = analyze_move(pos, line.move, limits.max_depth, line.pv.span(), length);
			line.length = length;
			++produced;
		}
		return produced;
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
		if (ply < max_ply) {
			m_pv_len[ply] = 0u;
		}
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
		if constexpr (EnableNullMove) {
			static constexpr NullMoveConfig null_config {};
			if (null_config.allowed(depth, checked, Rules::is_endgame(pos))) {
				Undo null_undo;
				Rules::make_null(pos, null_undo);
				const Score null_score = static_cast<Score>(-negamax(
				    pos, depth - 1u - null_config.reduction, static_cast<Score>(-beta),
				    static_cast<Score>(-beta + 1), ply + 1u));
				Rules::unmake_null(pos, null_undo);
				if (m_aborted) {
					return kScoreNone;
				}
				if (null_score >= beta) {
					return beta;
				}
			}
		}

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
				record_pv(ply, move);
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
		if (ply < max_ply) {
			m_pv_len[ply] = 0u;
		}
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

	/// Copia la PV del hijo a este ply tras elegir `move`.
	void record_pv(u32 ply, Move move) noexcept {
		if (ply >= max_ply) {
			return;
		}
		m_pv_table[ply][0] = move;
		u32 length = 1u;
		if (ply + 1u < max_ply) {
			const u32 child = m_pv_len[ply + 1u];
			for (u32 i = 0u; i < child && length < pv_max; ++i) {
				m_pv_table[ply][length] = m_pv_table[ply + 1u][i];
				++length;
			}
		}
		m_pv_len[ply] = length;
	}

	TranspositionTable<TtEntries> m_tt {};
	Ordering m_ordering {};
	eng::u64 m_nodes = 0u;
	eng::u64 m_max_nodes = 0u;
	bool m_aborted = false;
	const eng::parallel::StopToken* m_stop = nullptr;
	Move m_previous_best = kNoMove;
	eng::util::Array<eng::util::Array<Move, pv_max>, max_ply> m_pv_table {};
	eng::util::Array<u32, max_ply> m_pv_len {};
};

} // namespace eng::board
