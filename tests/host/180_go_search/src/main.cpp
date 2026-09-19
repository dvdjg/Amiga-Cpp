// ============================================================================
// Test HOST-180: evaluacion y busqueda de Go 9x9
// ============================================================================
//
// TUTORIAL. Con las reglas de HOST-179, el mismo buscador generico del engine juega
// al Go cambiando solo la policy (`GoRules` + `GoEval` + `GoOrdering`):
//
//   * EVALUACION: territorio (regiones vacias rodeadas por un solo color) +
//     capturas + penalizacion de grupos en atari. Devuelve la puntuacion desde el
//     bando al turno (negamax).
//   * BUSQUEDA: `GoSearcher` = `Searcher<GoRules, GoEval, GoOrdering>` (negamax +
//     alpha-beta + iterative deepening + quiescence). La quiescence poda por capturas
//     (el flag de captura viene en la jugada).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/153_go_search

#include <cstdio>

#include <eng/board/rules/go/rules.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::go;
using eng::u8;
using eng::u32;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_empty_eval_is_zero() {
	const Position pos = GoRules::initial();
	check(GoEval::evaluate(pos) == 0, "eval: tablero vacio = 0");
}

void test_corner_territory() {
	// Negro cierra la esquina (0,0) con (1,0) y (0,1); una piedra blanca lejana hace
	// que el resto del tablero sea neutral (lo tocan ambos colores).
	Position pos;
	pos.board[make_point(1u, 0u)] = kBlack;
	pos.board[make_point(0u, 1u)] = kBlack;
	pos.board[make_point(7u, 7u)] = kWhite;
	pos.to_move = kBlack;
	pos.key = compute_key(pos);
	check(evaluate_black(pos) > 0, "eval: la esquina cerrada es territorio negro");
}

void test_search_finds_capture() {
	// Negro en (4,4) con una libertad; blanco al turno debe capturar.
	Position pos;
	pos.board[make_point(4u, 4u)] = kBlack;
	pos.board[make_point(3u, 4u)] = kWhite;
	pos.board[make_point(5u, 4u)] = kWhite;
	pos.board[make_point(4u, 3u)] = kWhite;
	pos.to_move = kWhite;
	pos.key = compute_key(pos);

	GoSearcher searcher;
	GoSearcher::Limits limits {2u, 0u};
	const GoSearcher::Result result = searcher.search(pos, limits);
	check(GoRules::is_capture(result.best_move), "busqueda: elige una captura");
	check(go_point(result.best_move) == make_point(4u, 5u), "busqueda: captura en (4,5)");
}

void test_search_returns_legal_move() {
	GoSearcher searcher;
	Position pos = GoRules::initial();
	const GoSearcher::Result result = searcher.search(pos, {2u, 0u});
	check(!go_is_pass(result.best_move), "busqueda: no devuelve pase");
	MoveList legal;
	GoRules::generate_legal(pos, legal);
	bool found = false;
	for (eng::usize i = 0u; i < legal.size(); ++i) {
		if (go_point(legal[i]) == go_point(result.best_move)) {
			found = true;
		}
	}
	check(found, "busqueda: la jugada es legal");
	check(result.depth >= 1u, "busqueda: completa al menos una profundidad");
}

} // namespace

int main() {
	std::printf("Go 9x9: evaluacion y busqueda:\n");
	test_empty_eval_is_zero();
	test_corner_territory();
	test_search_finds_capture();
	test_search_returns_legal_move();

	if (g_fail == 0u) {
		std::printf("OK: Go (territorio, captura por busqueda y jugada legal)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
