// ============================================================================
// Test HOST-187: PGN (escritor) y libro de aperturas incorporado
// ============================================================================
//
// Valida dos piezas compartidas por la demo `123_chess_match` y la simulacion host
// `tools/board/selfplay.cpp`:
//
//   * `eng/board/rules/chess/pgn.hpp`         -> volcado PGN sin heap ni I/O
//   * `eng/board/rules/chess/opening_book.hpp`-> lineas de apertura incorporadas
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/160_pgn

#include <cstdio>
#include <cstring>

#include <eng/board/rules/chess/opening_book.hpp>
#include <eng/board/rules/chess/pgn.hpp>

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

void test_pgn_writes_expected_text() {
	char buffer[256];
	PgnWriter pgn {eng::Span<char> {buffer, sizeof(buffer)}};
	pgn.tag("Event", "Test");
	pgn.tag("White", "A");
	pgn.tag("Black", "B");
	pgn.tag("Result", "1-0");
	pgn.end_tags();
	pgn.move(1u, Color::White, eng::util::StringView {"e4"});
	pgn.comment(eng::util::StringView {"book"});
	pgn.move(1u, Color::Black, eng::util::StringView {"e5"});
	pgn.result(eng::util::StringView {"1-0"});

	const char* expected =
	    "[Event \"Test\"]\n"
	    "[White \"A\"]\n"
	    "[Black \"B\"]\n"
	    "[Result \"1-0\"]\n"
	    "\n"
	    "1. e4 {book} e5 1-0\n";
	check(pgn.ok(), "pgn: cabe en el buffer");
	buffer[pgn.length()] = '\0'; // el escritor no anade NUL; el llamador usa length()
	check(std::strcmp(buffer, expected) == 0, "pgn: texto exacto de una partida corta");
}

void test_pgn_truncation_is_reported() {
	char small[8];
	PgnWriter pgn {eng::Span<char> {small, sizeof(small)}};
	pgn.tag("Event", "Una partida con nombre largo");
	pgn.result(eng::util::StringView {"1-0"});
	check(!pgn.ok(), "pgn: buffer pequeno se marca truncado");
	check(pgn.length() > sizeof(small), "pgn: la longitud logica no se recorta");
}

void test_opening_book_has_entries() {
	BookEntry storage[32];
	const eng::u32 count = build_opening_book(eng::Span<BookEntry> {storage, 32u});
	check(count >= 5u, "libro: se construyen varias entradas");

	Position pos;
	set_start(pos);
	const Move e4 = find_move_uci(pos, eng::util::StringView {"e2e4"});
	check(!move_none(e4), "libro: find_move_uci encuentra e2e4");
	check(move_none(find_move_uci(pos, eng::util::StringView {"e2e5"})),
	      "libro: find_move_uci rechaza una jugada ilegal");

	Undo undo;
	make_move(pos, e4, undo);
	const BookProbe probe = probe_opening_book(pos, eng::Span<const BookEntry> {storage, count});
	check(probe.found, "libro: tras 1.e4 hay respuesta de libro");
	if (probe.found) {
		const eng::util::StringView name = opening_name(probe.name_id);
		check(!name.empty(), "libro: la entrada tiene nombre de apertura");
	}
}

} // namespace

int main() {
	std::printf("PGN y libro de aperturas:\n");
	test_pgn_writes_expected_text();
	test_pgn_truncation_is_reported();
	test_opening_book_has_entries();

	if (g_fail == 0u) {
		std::printf("OK: PGN (texto exacto, truncado) y libro de aperturas\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
