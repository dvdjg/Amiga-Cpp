// ============================================================================
// Test HOST-148: null-move, tiempo, PV/Multi-PV y analisis paralelo
// ============================================================================
//
// TUTORIAL. Cuatro piezas de "comportamiento moderno" sobre el buscador:
//
//   1. NULL-MOVE PRUNING (`search/pruning.hpp` + `ChessSearcherNull`). En posiciones
//      tranquilas se "pasa turno" y se busca con profundidad reducida; si aun asi
//      se supera beta, la rama se corta. Se desactiva en jaque y en finales.
//
//   2. GESTION DE TIEMPO (`search/time.hpp`). El reloj se inyecta como funcion,
//      porque el Amiga no tiene `chrono`: el juego pasa VBlank/CIA y el host pasa
//      `std::chrono`. El test usa un reloj falso determinista.
//
//   3. PV / MULTI-PV. `analyze_move` puntua una jugada con ventana completa y
//      devuelve su linea principal; `search_multi_pv` devuelve las N mejores
//      candidatas ordenadas (base del analisis tactico/estrategico).
//
//   4. ANALISIS PARALELO. `search_multi_pv` reparte la puntuacion de cada jugada de
//      la raiz con `eng::parallel::for_each_index`: secuencial en el Amiga, hilos
//      reales en el host, con resultado determinista (mismo ranking con 1 o N hilos).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/148_chess_b5

#include <cstdio>

#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/search/time.hpp>

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

// --- Reloj falso para el TimeManager ---
eng::u32 g_clock = 0u;
eng::u32 fake_now(void*) { return g_clock; }

void test_time_manager() {
	g_clock = 1000u;
	TimeManager manager {&fake_now, nullptr};
	manager.start();
	check(manager.elapsed_ms() == 0u, "tiempo: recien arrancado");

	g_clock = 1250u;
	check(manager.elapsed_ms() == 250u, "tiempo: 250 ms transcurridos");

	const TimeBudget soft {0u, 200u, 500u}; // soft=200, hard=500
	check(manager.soft_expired(soft, 0u), "tiempo: soft expirado a 250 ms");
	check(!manager.hard_expired(soft, 0u), "tiempo: hard aun no");
	g_clock = 1600u;
	check(manager.hard_expired(soft, 0u), "tiempo: hard expirado a 600 ms");

	const TimeBudget nodes {1000u, 0u, 0u};
	check(nodes.max_nodes == 1000u && manager.hard_expired(nodes, 1000u),
	      "tiempo: limite de nodos");
}

void test_null_move_finds_mate() {
	Position pos = position_from("7k/5Q2/6K1/8/8/8/8/8 w - - 0 1");
	ChessSearcherNull searcher;
	const ChessSearcherNull::Result result = searcher.search(pos, {3u, 0u});
	check(score_is_mate(result.score), "null-move: encuentra el mate");
}

void test_null_move_same_best_as_plain() {
	Position pos = ChessRules::initial();
	ChessSearcher plain;
	ChessSearcherNull with_null;
	const auto a = plain.search(pos, {4u, 0u});
	const auto b = with_null.search(pos, {4u, 0u});
	check(a.best_move == b.best_move, "null-move: misma mejor jugada que sin poda");
	check(b.depth == 4u, "null-move: completa la profundidad");
	(void)a.nodes;
	(void)b.nodes;
}

void test_pv_and_multi_pv() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;

	// PV de 1.e4: la jugada debe encabezar su propia linea.
	MoveList legal;
	generate_legal(pos, legal);
	Move e4 = kNoMove;
	for (eng::usize i = 0; i < legal.size(); ++i) {
		if (move_from(legal[i]) == make_square(4u, 1u) && move_to(legal[i]) == make_square(4u, 3u)) {
			e4 = legal[i];
		}
	}
	Move pv[ChessSearcher::pv_max] {};
	eng::u32 length = 0u;
	const Score score = searcher.analyze_move(pos, e4, 3u, pv, ChessSearcher::pv_max, length);
	check(!score_is_mate(score), "pv: 1.e4 no es mate a profundidad 3");
	check(length >= 1u && pv[0] == e4, "pv: la linea empieza por la jugada");
	check(length <= ChessSearcher::pv_max, "pv: respeta el maximo");

	// Multi-PV: tres lineas ordenadas por puntuacion.
	ChessSearcher::Line lines[3] {};
	const eng::u32 produced = searcher.search_multi_pv(pos, {3u, 0u}, 3u, lines, 3u, 1u);
	check(produced == 3u, "multi-pv: tres lineas");
	check(lines[0].score >= lines[1].score && lines[1].score >= lines[2].score,
	      "multi-pv: ordenadas por puntuacion");
	check(lines[0].length >= 1u && lines[0].pv[0] == lines[0].move,
	      "multi-pv: cada linea lleva su PV");
}

void test_parallel_is_deterministic() {
	Position pos = ChessRules::initial();
	ChessSearcher searcher;
	ChessSearcher::Line sequential[2] {};
	ChessSearcher::Line parallel[2] {};
	searcher.search_multi_pv(pos, {3u, 0u}, 2u, sequential, 2u, 1u);
	searcher.search_multi_pv(pos, {3u, 0u}, 2u, parallel, 2u, 4u);

	check(sequential[0].move == parallel[0].move, "paralelo: misma mejor jugada");
	check(sequential[0].score == parallel[0].score, "paralelo: misma puntuacion");
	check(sequential[1].move == parallel[1].move, "paralelo: misma segunda jugada");
}

} // namespace

int main() {
	std::printf("Ajedrez B5:\n");
	test_time_manager();
	test_null_move_finds_mate();
	test_null_move_same_best_as_plain();
	test_pv_and_multi_pv();
	test_parallel_is_deterministic();

	if (g_fail == 0u) {
		std::printf("OK: B5 (null-move, tiempo, PV/Multi-PV, analisis paralelo determinista)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
