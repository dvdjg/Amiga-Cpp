// ============================================================================
// Test HOST-183: variantes de condicion (King of the Hill y Three-check)
// ============================================================================
//
// TUTORIAL. Ademas de Chess960 (posicion inicial), hay variantes que cambian la
// CONDICION DE VICTORIA. Se implementan con un hook `variant_score` que el buscador
// consulta en cada nodo:
//
//   * KING OF THE HILL: gana quien lleva su rey a una de las 4 casillas centrales.
//   * THREE-CHECK: gana quien da 3 jaques.
//
// El ajedrez estandar/960 devuelve 0 (sin condicion) y no se ve afectado.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/156_chess_variants_cond

#include <cstdio>

#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/rules/chess/variant.hpp>

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

void test_king_of_the_hill() {
	// Blanco Kc4, negro Kf6: Kd4 lleva el rey al centro y gana.
	Position pos;
	(void)set_from_fen(pos, "8/8/5k2/8/2K5/8/8/8 w - - 0 1");
	KingOfTheHillSearcher searcher;
	const KingOfTheHillSearcher::Result result = searcher.search(pos, {1u, 0u});
	check(score_is_mate(result.score), "KOTH: encuentra la victoria");
	check(move_to(result.best_move) == make_square(3u, 3u),
	      "KOTH: mueve el rey a d4 (centro)");
}

void test_three_check() {
	// Blanco ya dio 2 jaques; Qd8+ da el tercero y gana.
	Position pos;
	(void)set_from_fen(pos, "4k3/8/8/8/8/8/8/3QK3 w - - 0 1");
	pos.checks[0] = 2u;
	ThreeCheckSearcher searcher;
	const ThreeCheckSearcher::Result result = searcher.search(pos, {1u, 0u});
	check(score_is_mate(result.score), "three-check: encuentra el 3er jaque");
}

void test_standard_unaffected() {
	Position pos;
	set_start(pos);
	ChessSearcher searcher;
	const ChessSearcher::Result result = searcher.search(pos, {2u, 0u});
	check(!score_is_mate(result.score), "standard: sin condicion de variante");
}

} // namespace

int main() {
	std::printf("Ajedrez: variantes de condicion:\n");
	test_king_of_the_hill();
	test_three_check();
	test_standard_unaffected();

	if (g_fail == 0u) {
		std::printf("OK: King of the Hill y Three-check (variant_score) sin afectar al standar\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
