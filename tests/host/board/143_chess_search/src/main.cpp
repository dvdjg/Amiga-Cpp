// ============================================================================
// Test HOST-143: buscador adversario (negamax + alpha-beta + quiescence)
// ============================================================================
//
// TUTORIAL. Este test valida `engine/include/eng/board/search/search.hpp` a traves
// del buscador de ajedrez `eng::board::ChessSearcher`.
//
// Como funciona el buscador, en una frase por pieza:
//
//   * NEGAMAX: el valor de una posicion es el maximo de los valores NEGADOS de las
//     respuestas del rival. Todo se mide desde el bando al turno.
//   * ALPHA-BETA: se mantiene una ventana [alpha, beta]; si una jugada supera beta
//     hay corte (el rival no la permitiria) y se deja de analizar.
//   * ITERATIVE DEEPENING: se busca a profundidad 1, 2, 3... y se guarda la mejor
//     jugada de cada nivel. Se puede cortar en cualquier momento sin perder todo.
//   * QUIESCENCE: al llegar al limite de profundidad no se evalua "en seco": se
//     siguen buscando capturas (y todas las jugadas si hay jaque) para no evaluar a
//     mitad de un intercambio (efecto horizonte).
//
// Los tests verifican: mate en 1, estabilidad en la posicion inicial, que una dama
// colgada se captura, que la quiescence resuelve una recaptura, y los presupuestos
// (nodos y cancelacion).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/143_chess_search

#include <cstdio>
#include <cstdlib>

#include <eng/board/rules/chess/rules.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::chess;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

Position position_from(const char* fen) {
	Position pos;
	(void)set_from_fen(pos, fen);
	return pos;
}

/// Aplica la mejor jugada y comprueba que la partida termina como `expected`.
Terminal apply_and_terminal(const Position& pos, Move move) {
	Position work = pos;
	Undo undo;
	make_move(work, move, undo);
	if (in_check(work, to_move(work))) {
		MoveList replies;
		generate_legal(work, replies);
		return replies.empty() ? Terminal::Checkmate : Terminal::None;
	}
	return Terminal::None;
}

void test_mate_in_one() {
	// White Qf7 + Kg6 vs Kh8: Qg7# es mate inmediato.
	Position pos = position_from("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
	ChessSearcher searcher;
	const ChessSearcher::Limits limits {2u, 0u};
	const ChessSearcher::Result result = searcher.search(pos, limits);

	check(score_is_mate(result.score), "mate en 1: la puntuacion es de mate");
	check(!move_none(result.best_move), "mate en 1: devuelve jugada");
	check(apply_and_terminal(pos, result.best_move) == Terminal::Checkmate,
	      "mate en 1: la jugada da jaque mate");
}

void test_start_position_is_balanced() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;
	const ChessSearcher::Limits limits {3u, 0u};
	const ChessSearcher::Result result = searcher.search(pos, limits);

	check(result.depth == 3u, "inicio: completa la profundidad pedida");
	check(!move_none(result.best_move), "inicio: devuelve jugada");
	check(!score_is_mate(result.score), "inicio: no hay mate");
	check(result.score > -100 && result.score < 100, "inicio: evaluacion equilibrada");
	check(result.nodes > 0u, "inicio: cuenta nodos");
}

void test_captures_hanging_queen() {
	// White Rd1 vs black Qd5 (sin defensa): Rxd5 gana la dama.
	Position pos = position_from("4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1");
	ChessSearcher searcher;
	const ChessSearcher::Limits limits {1u, 0u};
	const ChessSearcher::Result result = searcher.search(pos, limits);

	check(move_from(result.best_move) == make_square(3u, 0u) &&
	          move_to(result.best_move) == make_square(3u, 4u),
	      "dama colgada: elige Rxd5");
	check(result.score >= 400, "dama colgada: gana la dama (material +500)");
}

void test_quiescence_resolves_recapture() {
	// White Pd4 vs black Pe5+Pd6, reyes e1/e8: dxe5 gana un peon pero dxe5 recupera.
	// Sin quiescence, a profundidad 1 la evaluacion veria +0 tras la captura; con
	// quiescence ve el recapturo y devuelve la posicion real (peon abajo).
	Position pos = position_from("4k3/8/3p4/4p3/3P4/8/8/4K3 w - - 0 1");
	ChessSearcher searcher;
	const ChessSearcher::Limits limits {1u, 0u};
	const ChessSearcher::Result result = searcher.search(pos, limits);
	check(result.score <= -50, "quiescence: no se deja enganar por el peon defendido");
}

void test_node_budget_aborts_cleanly() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;
	ChessSearcher::Limits limits {20u, 500u};
	const ChessSearcher::Result result = searcher.search(pos, limits);

	check(result.aborted, "presupuesto: se marca abortado");
	check(!move_none(result.best_move), "presupuesto: conserva la mejor de la ultima profundidad");
	check(result.depth >= 1u, "presupuesto: al menos una profundidad completa");
	check(result.nodes >= 500u, "presupuesto: respeta el limite de nodos");
}

/// Con un presupuesto muy pequeno puede no completarse ni la profundidad 1; aun asi
/// el buscador debe devolver la mejor jugada parcialmente evaluada (asi la busqueda
/// por rebanadas de la demo no se queda sin jugada).
void test_tiny_budget_keeps_partial_move() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;
	ChessSearcher::Limits limits {12u, 8u};
	const ChessSearcher::Result result = searcher.search(pos, limits);

	check(result.aborted, "rebanada: se marca abortado con presupuesto minimo");
	check(!move_none(result.best_move), "rebanada: conserva una jugada parcial");

	bool legal = false;
	MoveList moves;
	generate_legal(pos, moves);
	for (eng::usize i = 0u; i < moves.size(); ++i) {
		if (moves[i] == result.best_move) {
			legal = true;
		}
	}
	check(legal, "rebanada: la jugada parcial es legal");
}

void test_stop_token_cancels() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;
	eng::parallel::StopSource source;
	source.request_stop();
	const eng::parallel::StopToken token = source.token();

	const ChessSearcher::Result result = searcher.search(pos, {20u, 0u}, token);
	check(result.aborted, "cancelacion: se marca abortado");
	check(result.depth == 0u, "cancelacion: no completa ninguna profundidad");
}

} // namespace

int main() {
	std::printf("Ajedrez: buscador:\n");
	test_mate_in_one();
	test_start_position_is_balanced();
	test_captures_hanging_queen();
	test_quiescence_resolves_recapture();
	test_node_budget_aborts_cleanly();
	test_tiny_budget_keeps_partial_move();
	test_stop_token_cancels();

	if (g_fail == 0u) {
		std::printf("OK: buscador (mate en 1, equilibrio, captura, quiescence, presupuesto, stop)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
