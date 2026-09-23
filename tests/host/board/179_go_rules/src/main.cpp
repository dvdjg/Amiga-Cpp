// ============================================================================
// Test HOST-179: reglas de Go 9x9 (grupos, captura, suicidio y ko)
// ============================================================================
//
// TUTORIAL. Go se juega sobre 81 puntos. Una piedra vive si su grupo (piedra mas
// adyacentes del mismo color, por flood fill) conserva alguna libertad (punto vacio
// adyacente). Reglas basicas que valida este test:
//
//   * CAPTURA: al colocar una piedra, los grupos rivales sin libertades se retiran.
//   * SUICIDIO: no se puede colocar una piedra cuyo grupo quede sin libertades
//     (salvo que la jugada capture).
//   * KO SIMPLE: si una jugada captura una unica piedra y la piedra colocada queda
//     con una sola libertad, no se puede recapturar de inmediato en ese punto.
//
// La jugada codifica el punto en 8 bits y un flag de captura (para la ordenacion y
// la quiescence sin simular).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/152_go_rules

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

bool has_move(const eng::board::MoveList& list, u8 point) {
	for (eng::usize i = 0u; i < list.size(); ++i) {
		if (go_point(list[i]) == point) {
			return true;
		}
	}
	return false;
}

void test_empty_board() {
	const Position pos = GoRules::initial();
	eng::board::MoveList legal;
	const u32 count = GoRules::generate_legal(pos, legal);
	check(count == 82u, "tablero vacio: 81 puntos + pase");
	check(pos.to_move == kBlack, "tablero vacio: mueve negro");
}

void test_capture() {
	// Negro en (4,4) con una libertad en (4,5); blanco juega y captura.
	Position pos;
	pos.board[make_point(4u, 4u)] = kBlack;
	pos.board[make_point(3u, 4u)] = kWhite;
	pos.board[make_point(5u, 4u)] = kWhite;
	pos.board[make_point(4u, 3u)] = kWhite;
	pos.to_move = kWhite;
	pos.key = compute_key(pos);

	eng::board::MoveList legal;
	GoRules::generate_legal(pos, legal);
	const u8 target = make_point(4u, 5u);
	check(has_move(legal, target), "captura: la jugada que rodea es legal");

	Undo undo;
	const Move move = go_move(target, true);
	GoRules::make(pos, move, undo);
	check(pos.board[make_point(4u, 4u)] == kEmpty, "captura: la piedra negra se retira");
	check(pos.captures[kWhite] == 1u, "captura: cuenta para blanco");
	GoRules::unmake(pos, move, undo);
	check(pos.board[make_point(4u, 4u)] == kBlack, "captura: unmake restaura");
}

void test_suicide_is_illegal() {
	// Blanco rodea (1,1); negro jugarlo alli seria suicidio (no captura nada).
	Position pos;
	pos.board[make_point(1u, 0u)] = kWhite;
	pos.board[make_point(0u, 1u)] = kWhite;
	pos.board[make_point(1u, 2u)] = kWhite;
	pos.board[make_point(2u, 1u)] = kWhite;
	pos.to_move = kBlack;
	pos.key = compute_key(pos);

	eng::board::MoveList legal;
	GoRules::generate_legal(pos, legal);
	check(!has_move(legal, make_point(1u, 1u)), "suicidio: prohibido");
	check(has_move(legal, make_point(5u, 5u)), "suicidio: el resto del tablero sigue legal");
}

void test_simple_ko() {
	// Forma de ko: negro captura una piedra blanca y queda con una sola libertad.
	Position pos;
	const u8 ko_point = make_point(1u, 1u);
	pos.board[ko_point] = kWhite;
	pos.board[make_point(3u, 1u)] = kWhite;
	pos.board[make_point(2u, 0u)] = kWhite;
	pos.board[make_point(2u, 2u)] = kWhite;
	pos.board[make_point(0u, 1u)] = kBlack;
	pos.board[make_point(1u, 0u)] = kBlack;
	pos.board[make_point(1u, 2u)] = kBlack;
	pos.to_move = kBlack;
	pos.key = compute_key(pos);

	const u8 capture_point = make_point(2u, 1u);
	eng::board::MoveList legal;
	GoRules::generate_legal(pos, legal);
	check(has_move(legal, capture_point), "ko: la captura es legal");

	Undo undo;
	GoRules::make(pos, go_move(capture_point, true), undo);
	check(pos.captures[kBlack] == 1u, "ko: negro captura una piedra");
	check(pos.ko_point == ko_point, "ko: se marca el punto prohibido");

	eng::board::MoveList reply;
	GoRules::generate_legal(pos, reply);
	check(!has_move(reply, ko_point), "ko: recapturar de inmediato es ilegal");

	// Tras jugar en otro sitio, el ko se levanta.
	Undo undo2;
	GoRules::make(pos, go_move(make_point(8u, 8u), false), undo2);
	eng::board::MoveList after;
	GoRules::generate_legal(pos, after);
	check(has_move(after, ko_point) || pos.ko_point == kNoPoint, "ko: se levanta al jugar fuera");
}

} // namespace

int main() {
	std::printf("Go 9x9: reglas:\n");
	test_empty_board();
	test_capture();
	test_suicide_is_illegal();
	test_simple_ko();

	if (g_fail == 0u) {
		std::printf("OK: Go (81 jugadas, captura, suicidio y ko simple)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
