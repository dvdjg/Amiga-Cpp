// ============================================================================
// Test HOST-144: ordenacion de jugadas y tabla de transposicion
// ============================================================================
//
// TUTORIAL. Dos piezas que hacen eficiente al buscador de HOST-143:
//
//   1. ORDENACION (`rules/chess/ordering.hpp`). Alpha-beta poda mas cuanto antes
//      aparece la mejor jugada. Se ordena por:
//        - la jugada de la TT primero;
//        - capturas por MVV-LVA (capturar la victima mas valiosa con el atacante
//          mas barato);
//        - killers (jugadas tranquilas que ya cortaron en ese ply);
//        - history (frecuencia con que una jugada provoco cortes).
//
//   2. TABLA DE TRANSPOSICION (`search/tt.hpp`). Guarda clave -> (jugada, score,
//      profundidad, tipo de cota). Su entrada mide 12 bytes EXACTOS; con el
//      presupuesto del motor se dimensiona en `init`. El test comprueba el
//      empaquetado, el sondeo/escritura y que una busqueda repetida con la TT
//      caliente explora MENOS nodos.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/board/144_chess_tt_ordering

#include <cstdio>

#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/search/tt.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::chess;
using eng::u32;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_tt_entry_layout() {
	// Empaquetado fijo del motor: si cambia, el presupuesto de RAM deja de cuadrar.
	static_assert(sizeof(TtEntry) == 12u, "TtEntry = 12 bytes");
	static_assert(TranspositionTable<1024>::entry_count == 1024u, "entradas");
	check(TranspositionTable<1024>::entry_bytes == 12u, "tt: entry_bytes = 12");
}

void test_tt_store_probe() {
	TranspositionTable<16> tt;
	tt.clear();

	Move move_out = kNoMove;
	Score score_out = 0;
	u32 depth_out = 0u;
	TtFlag flag_out = TtFlag::None;
	check(!tt.probe(0x1234u, move_out, score_out, depth_out, flag_out), "tt: vacia no encuentra");

	const Move move = make_move(make_square(4u, 1u), make_square(4u, 3u));
	tt.store(0x1234u, 7u, TtFlag::Exact, 42, move);
	check(tt.probe(0x1234u, move_out, score_out, depth_out, flag_out), "tt: encuentra lo guardado");
	check(move_out == move && score_out == 42 && depth_out == 7u && flag_out == TtFlag::Exact,
	      "tt: campos exactos");

	// La profundidad va en los 6 bits altos y el flag en los 2 bajos.
	tt.store(0x1234u, 63u, TtFlag::Beta, -5, kNoMove);
	(void)tt.probe(0x1234u, move_out, score_out, depth_out, flag_out);
	check(depth_out == 63u && flag_out == TtFlag::Beta, "tt: profundidad saturada y flag Beta");
}

void test_tt_replacement_by_index() {
	// Dos claves que caen en el mismo indice (Entries=16): la segunda pisa la primera.
	TranspositionTable<16> tt;
	const u32 key1 = 0x00000005u;
	const u32 key2 = key1 + 16u; // mismo (key & 15)
	tt.store(key1, 3u, TtFlag::Exact, 10, kNoMove);
	tt.store(key2, 3u, TtFlag::Exact, 20, kNoMove);

	Move move_out = kNoMove;
	Score score_out = 0;
	u32 depth_out = 0u;
	TtFlag flag_out = TtFlag::None;
	check(!tt.probe(key1, move_out, score_out, depth_out, flag_out),
	      "tt: la clave vieja se reemplaza");
	check(tt.probe(key2, move_out, score_out, depth_out, flag_out) && score_out == 20,
	      "tt: la clave nueva permanece");
}

void test_mvv_lva_ordering() {
	// White Rd1 y Pe4; negras Qd5 y Pf5. Capturas: Rxd5 (dama) y exf5 (peon).
	// MVV-LVA debe poner Rxd5 primero.
	Position pos;
	(void)set_from_fen(pos, "4k3/8/8/3q1p2/4P3/8/8/3RK3 w - - 0 1");
	MoveList moves;
	generate_legal(pos, moves);

	ChessOrdering ordering;
	ordering.order(pos, moves, kNoMove, 0u);
	check(moves.size() >= 3u, "orden: hay jugadas");
	// Hay dos capturas de la dama (peon exd5 y torre Rxd5) y una de peon (exf5).
	// MVV-LVA pone primero las de dama y, entre ellas, el atacante mas barato.
	check(move_to(moves[0]) == make_square(3u, 4u) && move_from(moves[0]) == make_square(4u, 3u),
	      "orden: el peon captura la dama (atacante mas barato)");
	check(move_to(moves[1]) == make_square(3u, 4u) && move_from(moves[1]) == make_square(3u, 0u),
	      "orden: la torre captura la dama en segundo lugar");
}

void test_killer_recorded() {
	Position pos = ChessRules::initial();
	ChessOrdering ordering;
	MoveList moves;
	generate_legal(pos, moves);

	// Simula que una jugada tranquila provoco un corte beta en el ply 2.
	const Move quiet = moves[0];
	ordering.on_beta(pos, quiet, 2u, 4u);
	check(ordering.killers[2][0] == quiet, "killer: se registra el corte");
	check(ordering.score(pos, quiet, kNoMove, 2u) >= 399000,
	      "killer: puntua por encima de una jugada normal");
}

void test_tt_warm_search_uses_fewer_nodes() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;
	const ChessSearcher::Limits limits {5u, 0u};

	const ChessSearcher::Result first = searcher.search(pos, limits);
	const ChessSearcher::Result second = searcher.search(pos, limits);

	check(first.depth == 5u && second.depth == 5u, "tt: ambas busquedas completan");
	check(second.nodes < first.nodes, "tt: la segunda busqueda explora menos nodos");
	check(second.best_move == first.best_move, "tt: mismo resultado con la TT caliente");
}

} // namespace

int main() {
	std::printf("Ajedrez: TT y ordenacion:\n");
	test_tt_entry_layout();
	test_tt_store_probe();
	test_tt_replacement_by_index();
	test_mvv_lva_ordering();
	test_killer_recorded();
	test_tt_warm_search_uses_fewer_nodes();

	if (g_fail == 0u) {
		std::printf("OK: TT (12 B, sondeo, reemplazo, reuso) y ordenacion (MVV-LVA, killer)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
