// ============================================================================
// Test HOST-142: notacion SAN y UCI de ajedrez
// ============================================================================
//
// TUTORIAL. Este test valida `engine/include/eng/board/rules/chess/notation.hpp`.
// SAN (Standard Algebraic Notation) es como se escriben las jugadas para humanos y
// para los libros: pieza + desambiguacion + captura + destino + promocion + jaque.
//
//   e4            peon de e2 a e4 (el peon no lleva letra)
//   Nf3           caballo a f3
//   exd5          peon de la columna e captura en d5
//   a8=Q          peon corona dama
//   Nab3 / Ncb3   dos caballos llegan a b3: se desambigua por columna
//   O-O / O-O-O   enroque corto / largo
//   Qh4#          jaque mate (sufijo #; jaque simple es +)
//
// `to_uci` produce `e2e4`/`e7e8q`, util para depurar y hablar con herramientas.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/board/142_chess_notation

#include <cstdio>
#include <cstring>

#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/fen.hpp>
#include <eng/board/rules/chess/movegen.hpp>
#include <eng/board/rules/chess/notation.hpp>

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

Move find_move(const Position& pos, Square from, Square to) {
	MoveList legal;
	generate_legal(pos, legal);
	for (eng::usize i = 0; i < legal.size(); ++i) {
		if (move_from(legal[i]) == from && move_to(legal[i]) == to) {
			return legal[i];
		}
	}
	return kNoMove;
}

void check_san(const Position& pos, Square from, Square to, eng::util::StringView expected,
               const char* what) {
	const Move move = find_move(pos, from, to);
	check(!move_none(move), what);
	if (move_none(move)) {
		return;
	}
	char text[16];
	const eng::usize n = to_san(pos, move, eng::Span<char> {text, sizeof(text)});
	if (eng::util::StringView(text, n) != expected) {
		std::printf("[FAIL] %s: '%s' (esperado '%s')\n", what, text, expected.data());
		++g_fail;
	}
}

void test_pawn_and_knight() {
	const Position start = position_from("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	check_san(start, make_square(4u, 1u), make_square(4u, 3u), "e4", "san: peon e4");
	check_san(start, make_square(6u, 0u), make_square(5u, 2u), "Nf3", "san: caballo a f3");
}

void test_castling() {
	const Position pos = position_from("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
	check_san(pos, make_square(4u, 0u), make_square(6u, 0u), "O-O", "san: enroque corto");
	check_san(pos, make_square(4u, 0u), make_square(2u, 0u), "O-O-O", "san: enroque largo");
}

void test_capture() {
	// 1.e4 d5 2.exd5
	Position pos = position_from("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	Undo undo;
	Move move = find_move(pos, make_square(4u, 1u), make_square(4u, 3u));
	make_move(pos, move, undo);
	move = find_move(pos, make_square(3u, 6u), make_square(3u, 4u));
	make_move(pos, move, undo);
	check_san(pos, make_square(4u, 3u), make_square(3u, 4u), "exd5", "san: captura de peon");
}

void test_promotion() {
	const Position pos = position_from("8/P6k/8/8/8/8/8/K7 w - - 0 1");
	check_san(pos, make_square(0u, 6u), make_square(0u, 7u), "a8=Q", "san: promocion a dama");
}

void test_disambiguation() {
	// Dos caballos (a1 y c1) pueden ir a b3: columna distinta -> Nab3 / Ncb3.
	const Position pos = position_from("8/8/8/8/8/8/8/N1N1K3 w - - 0 1");
	check_san(pos, make_square(0u, 0u), make_square(1u, 2u), "Nab3", "san: desambiguacion por columna (a)");
	check_san(pos, make_square(2u, 0u), make_square(1u, 2u), "Ncb3", "san: desambiguacion por columna (c)");
}

void test_mate_suffix() {
	// Mate del loco: ... Qh4# (la dama negra de d8 a h4).
	const Position pos =
	    position_from("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2");
	check_san(pos, make_square(3u, 7u), make_square(7u, 3u), "Qh4#", "san: mate con sufijo #");
}

void test_uci() {
	const Position start = position_from("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	const Move e4 = find_move(start, make_square(4u, 1u), make_square(4u, 3u));
	char text[8];
	eng::usize n = to_uci(e4, eng::Span<char> {text, sizeof(text)});
	check(eng::util::StringView(text, n) == eng::util::StringView("e2e4"), "uci: e2e4");

	const Position promo = position_from("8/P6k/8/8/8/8/8/K7 w - - 0 1");
	const Move a8q = find_move(promo, make_square(0u, 6u), make_square(0u, 7u));
	n = to_uci(a8q, eng::Span<char> {text, sizeof(text)});
	check(eng::util::StringView(text, n) == eng::util::StringView("a7a8q"), "uci: a7a8q");
}

} // namespace

int main() {
	std::printf("Ajedrez: notacion:\n");
	test_pawn_and_knight();
	test_castling();
	test_capture();
	test_promotion();
	test_disambiguation();
	test_mate_suffix();
	test_uci();

	if (g_fail == 0u) {
		std::printf("OK: ajedrez (SAN, enroque, captura, promocion, desambiguacion, mate, UCI)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
