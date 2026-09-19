#pragma once

/// \file tournament.hpp
/// **Torneos rápidos** de ajedrez: juega partidas completas entre dos buscadores
/// (o el mismo en distinta configuración) con un presupuesto de nodos por jugada y
/// arranques de variante (p. ej. Chess960 con semillas distintas). Devuelve el
/// resultado de cada partida y el marcador agregado.
///
/// Pensado para mediciones rápidas en host: la misma infraestructura sirve para
/// validar cambios de evaluación/búsqueda o para enfrentar profundidades/semillas.
/// En el Amiga se puede usar para un modo "el motor juega solo".
///
/// Verificación: HOST-181 y herramienta `tools/board/arena.cpp`.

#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/rules/chess/variant.hpp>

namespace eng::board {

/// Resultado de una partida. `winner`: 0 blancas, 1 negras, 2 tablas, 0xff sin fin.
struct MatchOutcome {
	u8 winner = 0xffu;
	u32 plies = 0u;
};

/// Marcador agregado de un torneo.
struct ArenaResult {
	u32 white_wins = 0u;
	u32 black_wins = 0u;
	u32 draws = 0u;
	u32 unfinished = 0u;
	u32 games = 0u;

	[[nodiscard]] u32 finished() const { return white_wins + black_wins + draws; }
};

/// Juega una partida desde `pos`. Cada bando usa su buscador con `depth` y
/// `node_budget` (0 = sin límite) por jugada; `max_plies` acota la partida.
template <class Searcher>
[[nodiscard]] MatchOutcome play_game(Searcher& white, Searcher& black, chess::Position pos,
                                     u32 depth, eng::u64 node_budget, u32 max_plies) {
	white.clear();
	black.clear();
	MatchOutcome out;
	for (u32 ply = 0u; ply < max_plies; ++ply) {
		const Terminal terminal = chess::terminal(pos);
		if (terminal == Terminal::Checkmate) {
			out.winner = (chess::to_move(pos) == Color::White) ? 1u : 0u;
			out.plies = ply;
			return out;
		}
		if (terminal != Terminal::None) {
			out.winner = 2u;
			out.plies = ply;
			return out;
		}
		Searcher& engine = (chess::to_move(pos) == Color::White) ? white : black;
		const typename Searcher::Limits limits {depth, node_budget};
		const typename Searcher::Result result = engine.search(pos, limits);
		if (result.depth == 0u || result.best_move == kNoMove) {
			out.winner = 2u;
			out.plies = ply;
			return out;
		}
		chess::Undo undo;
		chess::make_move(pos, result.best_move, undo);
	}
	out.winner = 0xffu;
	out.plies = max_plies;
	return out;
}

/// Torneo rápido de `games` partidas con arranques de `variant` (semilla creciente).
inline ArenaResult arena_chess(u32 games, u32 depth, eng::u64 node_budget,
                               chess::ChessVariant variant, u16 seed_base, u32 max_plies) {
	ArenaResult result;
	for (u32 g = 0u; g < games; ++g) {
		chess::Position pos =
		    chess::initial_position(variant, static_cast<u16>(seed_base + static_cast<u16>(g)));
		ChessSearcher white;
		ChessSearcher black;
		const MatchOutcome outcome = play_game(white, black, pos, depth, node_budget, max_plies);
		switch (outcome.winner) {
		case 0u: ++result.white_wins; break;
		case 1u: ++result.black_wins; break;
		case 2u: ++result.draws; break;
		default: ++result.unfinished; break;
		}
		++result.games;
	}
	return result;
}

} // namespace eng::board
