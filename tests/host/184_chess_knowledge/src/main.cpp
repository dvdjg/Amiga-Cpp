// ============================================================================
// Test HOST-184: conocimiento consumido por el motor (libro + finales)
// ============================================================================
//
// TUTORIAL. Cierra el ciclo del conocimiento construido antes:
//
//   * LIBRO DE APERTURAS: `probe_opening_book(pos, book)` consulta el libro por la
//     clave Zobrist de la posicion y devuelve la jugada (y el nombre).
//   * FINALES TEORICOS EN LA EVALUACION: `evaluate` reconoce finales ganados
//     conocidos (KRK/KQK/KBNK/KQvKR) y devuelve una ventaja grande, para que la
//     busqueda vaya directa a la conversion.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/157_chess_knowledge

#include <cstdio>

#include <eng/board/knowledge/book.hpp>
#include <eng/board/rules/chess/opening.hpp>
#include <eng/board/rules/chess/rules.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::chess;
using eng::u8;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_opening_book() {
	Position start;
	set_start(start);
	MoveList legal;
	generate_legal(start, legal);
	Move e4 = kNoMove;
	for (eng::usize i = 0u; i < legal.size(); ++i) {
		if (move_from(legal[i]) == make_square(4u, 1u) &&
		    move_to(legal[i]) == make_square(4u, 3u)) {
			e4 = legal[i];
		}
	}
	check(!move_none(e4), "libro: hay 1.e4");

	BookEntry storage[1];
	BookBuilder builder {eng::Span<BookEntry> {storage, 1u}};
	check(builder.add(start.key, e4, 20, 7u), "libro: agrega la linea");
	builder.finalize();

	const BookProbe probe = probe_opening_book(start, builder.entries());
	check(probe.found && probe.move == e4 && probe.name_id == 7u,
	      "libro: la posicion inicial esta en el libro");
}

void test_endgame_in_eval() {
	Position pos;
	(void)set_from_fen(pos, "8/8/8/4k3/8/8/4K3/7R w - - 0 1"); // K+R vs K, blancas
	check(evaluate(pos) > 1000, "finales: K+R vs K favorece a quien tiene la torre");
	pos.side = static_cast<u8>(Color::Black);
	pos.key = compute_key(pos);
	check(evaluate(pos) < -1000, "finales: desde el bando perdedor es negativo");
}

} // namespace

int main() {
	std::printf("Ajedrez: conocimiento en el motor:\n");
	test_opening_book();
	test_endgame_in_eval();

	if (g_fail == 0u) {
		std::printf("OK: libro de aperturas (sonda) y finales en la evaluacion\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
