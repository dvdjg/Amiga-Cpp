// ============================================================================
// Test HOST-141: repeticion (3 veces) y finales teoricos de ajedrez
// ============================================================================
//
// TUTORIAL. Este test valida `engine/include/eng/board/rules/chess/history.hpp` y
// `endgame.hpp`:
//
//   1. HISTORIAL / REPETICION. La clave Zobrist de una posicion incluye turno,
//      enroques y al paso. Si se repite tres veces, la partida es tablas. El
//      historial solo necesita mirar hacia atras la ventana de los 50 movimientos
//      (`halfmove + 1`), porque una repeticion no cruza una captura ni un peon.
//      Aqui se repite el "bailoteo" de caballos Nf3 Nf6 Ng1 Ng8 dos veces: tras 4
//      plies se vuelve a la posicion inicial (2 veces) y tras 8 plies son 3.
//
//   2. FINALES TEORICOS. `probe_endgame` reconoce por MATERIAL el final y su
//      resultado elemental; para K+P vs K aplica la REGLA DEL CUADRADO: el rey
//      defensor alcanza la promocion si su distancia de Chebyshev a la casilla de
//      coronacion no supera los avances del peon.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/board/141_chess_draw_endgame

#include <cstdio>

#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/endgame.hpp>
#include <eng/board/rules/chess/fen.hpp>
#include <eng/board/rules/chess/history.hpp>
#include <eng/board/rules/chess/movegen.hpp>
#include <eng/board/rules/chess/rules.hpp>

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

Position position_from(const char* fen) {
	Position pos;
	(void)set_from_fen(pos, fen);
	return pos;
}

/// Busca en las jugadas legales la que va de `from` a `to` (sin promocion). Es el
/// ayudante minimo para "jugar" desde el test sin un parser de SAN.
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

/// Juega `from -> to` actualizando el historial (lo que hace el motor en partida).
void play(Position& pos, PositionHistory<512>& history, Square from, Square to) {
	const Move move = find_move(pos, from, to);
	check(!move_none(move), "history: la jugada existe");
	if (move_none(move)) {
		return;
	}
	Undo undo;
	make_move(pos, move, undo);
	history.push(pos.key);
}

void test_threefold_repetition() {
	Position pos = position_from("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	PositionHistory<512> history;
	history.push(pos.key);

	// Nf3 Nf6 Ng1 Ng8 (primera vuelta): la posicion vuelve a ser la inicial.
	play(pos, history, make_square(6u, 0u), make_square(5u, 2u)); // Ng1-f3
	play(pos, history, make_square(6u, 7u), make_square(5u, 5u)); // Ng8-f6
	play(pos, history, make_square(5u, 2u), make_square(6u, 0u)); // Nf3-g1
	play(pos, history, make_square(5u, 5u), make_square(6u, 7u)); // Nf6-g8

	const Position initial = position_from("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	check(pos.key == initial.key, "repeticion: tras 4 plies vuelve a la inicial");
	check(history.repetitions(pos.key, static_cast<u32>(pos.halfmove) + 1u) == 2u,
	      "repeticion: dos apariciones (inicial + actual)");
	check(terminal(pos, history) == Terminal::None, "repeticion: dos veces aun no es tablas");

	// Segunda vuelta: tres apariciones -> tablas.
	play(pos, history, make_square(6u, 0u), make_square(5u, 2u));
	play(pos, history, make_square(6u, 7u), make_square(5u, 5u));
	play(pos, history, make_square(5u, 2u), make_square(6u, 0u));
	play(pos, history, make_square(5u, 5u), make_square(6u, 7u));

	check(history.repetitions(pos.key, static_cast<u32>(pos.halfmove) + 1u) == 3u,
	      "repeticion: tres apariciones");
	check(terminal(pos, history) == Terminal::Repetition, "repeticion: tablas por 3 veces");
}

void test_repetition_window_resets() {
	// Tras un movimiento de peon la ventana de repeticion se corta: la clave inicial
	// ya no cuenta aunque coincida en piezas (el peon cambio la posicion).
	Position pos = position_from("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	PositionHistory<512> history;
	history.push(pos.key);
	play(pos, history, make_square(4u, 1u), make_square(4u, 3u)); // e2-e4 (irreversible)
	check(history.repetitions(pos.key, static_cast<u32>(pos.halfmove) + 1u) == 1u,
	      "repeticion: el peon abre una ventana nueva");
}

void test_endgame_draw_material() {
	EndgameProbe bare = probe_endgame(position_from("8/8/4k3/8/8/4K3/8/8 w - - 0 1"));
	check(bare.kind == EndgameKind::KvK && bare.known && bare.is_draw, "final: K vs K tablas");

	EndgameProbe bishop = probe_endgame(position_from("8/8/4k3/8/8/4K3/8/6B1 w - - 0 1"));
	check(bishop.kind == EndgameKind::KBvK && bishop.is_draw, "final: K+B vs K tablas");

	EndgameProbe same_color =
	    probe_endgame(position_from("5b2/8/4k3/8/8/4K3/8/2B5 w - - 0 1"));
	check(same_color.kind == EndgameKind::KBvKB && same_color.is_draw,
	      "final: K+B vs K+B mismo color tablas");

	EndgameProbe rook = probe_endgame(position_from("8/8/8/4k3/8/8/4K3/7R w - - 0 1"));
	check(rook.kind == EndgameKind::KRvK && rook.is_win, "final: K+R vs K ganado");

	EndgameProbe queen = probe_endgame(position_from("8/8/8/4k3/8/8/4K3/6Q1 w - - 0 1"));
	check(queen.kind == EndgameKind::KQvK && queen.is_win, "final: K+Q vs K ganado");
}

void test_endgame_kpk_rule_of_square() {
	// Peon e2, rey negro en e8: esta DENTRO del cuadrado (distancia a e8 = 0) -> tablas.
	EndgameProbe caught = probe_endgame(position_from("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
	check(caught.kind == EndgameKind::KPvK && caught.known && caught.is_draw,
	      "KPK: rey en el cuadrado -> tablas");

	// Peon e2, rey negro en h1: fuera del cuadrado (distancia a e8 = 7 > 6) -> corona.
	EndgameProbe promotes = probe_endgame(position_from("8/8/8/8/8/8/4P3/4K2k w - - 0 1"));
	check(promotes.kind == EndgameKind::KPvK && promotes.known && promotes.is_win,
	      "KPK: rey fuera del cuadrado -> gana");
}

} // namespace

int main() {
	std::printf("Ajedrez: repeticion y finales:\n");
	test_threefold_repetition();
	test_repetition_window_resets();
	test_endgame_draw_material();
	test_endgame_kpk_rule_of_square();

	if (g_fail == 0u) {
		std::printf("OK: ajedrez (repeticion 3x, ventana, finales teoricos, regla del cuadrado)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
