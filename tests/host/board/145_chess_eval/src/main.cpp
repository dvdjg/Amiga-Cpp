// ============================================================================
// Test HOST-145: evaluacion de ajedrez y rasgos de desarrollo
// ============================================================================
//
// TUTORIAL. La evaluacion (`eval/chess_eval.hpp`) es lo que la busqueda usa en las
// hojas. Es ligera y entera (centipeones, sin float):
//
//   material  +  PST (tablas pieza-casilla por centralizacion)  +  movilidad
//   +  estructura de peones (doblados/aislados)  +  desarrollo/seguridad del rey
//
// `evaluate` devuelve la puntuacion DESDE EL BANDO AL TURNO (negamax); por eso una
// posicion simetrica vale 0 y, tras 1.e4 (que mejora a las blancas), la puntuacion
// desde el punto de vista de las negras es NEGATIVA.
//
// Los rasgos (`eval/features.hpp`) se extraen una sola vez y los comparten la
// evaluacion y el futuro explicador en lenguaje natural: piezas sin desarrollar,
// dama prematura, rey en el centro, enroques, torres en columnas abiertas y fase.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/145_chess_eval

#include <cstdio>

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

void test_symmetric_start_is_zero() {
	const Position start = ChessRules::initial();
	check(evaluate(start) == 0, "eval: la posicion inicial es 0");
}

void test_central_pawn_helps_white() {
	// 1.e4: desde el punto de vista de las negras (al turno) la evaluacion baja.
	Position pos = ChessRules::initial();
	Undo undo;
	const Move e4 = find_move(pos, make_square(4u, 1u), make_square(4u, 3u));
	make_move(pos, e4, undo);
	check(evaluate(pos) < 0, "eval: 1.e4 mejora a las blancas (negativo para negras)");
}

void test_knight_centralization() {
	const Position corner = position_from("4k3/8/8/8/8/8/8/N3K3 w - - 0 1"); // Na1
	const Position center = position_from("4k3/8/8/8/4N3/8/8/4K3 w - - 0 1"); // Ne4
	check(evaluate_white(center).total > evaluate_white(corner).total,
	      "eval: un caballo centralizado vale mas que uno en la esquina");
}

void test_development_features() {
	const DevelopmentFeatures start = extract_development(ChessRules::initial());
	check(start.undeveloped_minors[0] == 4u, "rasgos: inicio 4 menores sin salir");
	check(start.undeveloped_majors[0] == 3u, "rasgos: inicio 3 mayores sin mover (2 torres + dama)");
	check(!start.queen_moved_early[0], "rasgos: la dama no se movio");
	check(start.phase == GamePhase::Opening, "rasgos: fase de apertura");

	Position after_nf3 = ChessRules::initial();
	Undo undo;
	make_move(after_nf3, find_move(after_nf3, make_square(6u, 0u), make_square(5u, 2u)), undo);
	const DevelopmentFeatures mid = extract_development(after_nf3);
	check(mid.undeveloped_minors[0] == 3u, "rasgos: 1.Cf3 desarrolla un menor (quedan 3)");
}

void test_queen_moved_early() {
	// Tras 1.e4 e5 2.Qh5 la dama blanca sale antes de tiempo.
	const Position pos =
	    position_from("rnbqkbnr/pppp1ppp/8/4p2Q/4P3/8/PPPP1PPP/RNB1KBNR b KQkq - 1 3");
	const DevelopmentFeatures features = extract_development(pos);
	check(features.queen_moved_early[0], "rasgos: dama prematura detectada");
	check(!features.queen_moved_early[1], "rasgos: la dama negra sigue en su sitio");
}

void test_king_in_center_middlegame() {
	// En la jugada 12 y sin enrocar, el rey en e1/e8 es un riesgo.
	const Position pos = position_from("r3k2r/pppppppp/8/8/8/8/PPPPPPPP/R3K2R w KQkq - 0 12");
	const DevelopmentFeatures features = extract_development(pos);
	check(features.king_in_center[0] && features.king_in_center[1],
	      "rasgos: reyes en el centro en medio juego");
	check(features.can_castle_king[0] && features.can_castle_queen[0],
	      "rasgos: derechos de enroque presentes");
}

} // namespace

int main() {
	std::printf("Ajedrez: evaluacion:\n");
	test_symmetric_start_is_zero();
	test_central_pawn_helps_white();
	test_knight_centralization();
	test_development_features();
	test_queen_moved_early();
	test_king_in_center_middlegame();

	if (g_fail == 0u) {
		std::printf("OK: evaluacion (simetria, centralizacion, rasgos de desarrollo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
