// ============================================================================
// Test HOST-182: Go completo (pase/dos pases, superko y patrones de apertura)
// ============================================================================
//
// TUTORIAL. Cierra las reglas de Go que faltaban:
//
//   * PASE: pasar turno es legal; DOS pases consecutivos terminan la partida
//     (`is_over`/`terminal == GameEnded`).
//   * SUPERKO: no se puede recrear una posicion anterior (historial de claves).
//     Cubre el ko simple y las repeticiones recientes.
//   * APERTURA: `opening_move` propone puntos estrella (4-4) o komoku (3-4).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/155_go_complete

#include <cstdio>

#include <eng/board/knowledge/patterns.hpp>
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

bool has_move(const eng::board::MoveList& list, Move move) {
	for (eng::usize i = 0u; i < list.size(); ++i) {
		if (list[i] == move) {
			return true;
		}
	}
	return false;
}

void test_pass_and_two_passes() {
	Position pos = GoRules::initial();
	Undo undo;
	GoRules::make(pos, kGoPass, undo);
	check(!GoRules::is_over(pos), "pase: uno no termina");
	GoRules::make(pos, kGoPass, undo);
	check(GoRules::is_over(pos), "pase: dos consecutivos terminan");
	check(GoRules::terminal(pos) == Terminal::GameEnded, "pase: terminal GameEnded");
}

void test_superko_helper() {
	Position pos;
	pos.key = 123u;
	push_history(pos, 456u);
	check(position_repeated(pos, 123u), "superko: la posicion actual cuenta");
	check(position_repeated(pos, 456u), "superko: el historial cuenta");
	check(!position_repeated(pos, 789u), "superko: una clave nueva no");
}

void test_legal_includes_pass() {
	const Position pos = GoRules::initial();
	eng::board::MoveList legal;
	const u32 count = GoRules::generate_legal(pos, legal);
	check(count == 82u, "legal: 81 puntos + pase");
	check(has_move(legal, kGoPass), "legal: incluye el pase");
}

void test_opening_pattern() {
	const Position empty = GoRules::initial();
	const Move first = opening_move(empty);
	check(!go_is_pass(first), "apertura: propone jugada");
	check(go_point(first) == make_point(2u, 2u) || go_point(first) == make_point(6u, 2u) ||
	          go_point(first) == make_point(2u, 6u) || go_point(first) == make_point(6u, 6u),
	      "apertura: punto estrella");

	Position pos = empty;
	pos.board[make_point(2u, 2u)] = kBlack;
	pos.key = compute_key(pos);
	const Move second = opening_move(pos);
	check(go_point(second) != make_point(2u, 2u), "apertura: evita el punto ocupado");
}

} // namespace

int main() {
	std::printf("Go 9x9: pase, superko y apertura:\n");
	test_pass_and_two_passes();
	test_superko_helper();
	test_legal_includes_pass();
	test_opening_pattern();

	if (g_fail == 0u) {
		std::printf("OK: Go (pase/dos pases, superko y patrones de apertura)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
