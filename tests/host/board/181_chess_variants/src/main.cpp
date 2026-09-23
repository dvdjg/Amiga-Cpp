// ============================================================================
// Test HOST-181: variantes (Chess960) y torneos rapidos
// ============================================================================
//
// TUTORIAL. Ademas del ajedrez estandar, el motor soporta la variante de "piezas
// descolocadas" mas habitual: **Chess960 / Fischer Random**. La fila inicial se
// baraja con las restricciones de siempre (alfiles en colores opuestos, rey entre
// torres) y el ENROQUE se generaliza: el rey va a g/c y la torre a f/d aunque las
// piezas empiecen en columnas arbitrarias.
//
// `tournament.hpp` juega partidas completas con presupuesto de nodos por jugada y
// arranques de variante, para torneos rapidos y medidas (p. ej. semillas distintas).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/board/181_chess_variants

#include <cstdio>

#include <eng/board/tournament.hpp>
#include <eng/board/rules/chess/variant.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::chess;
using eng::u8;
using eng::u16;
using eng::u32;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_chess960_arrangements() {
	// Las 960 posiciones cumplen: alfiles en colores opuestos y rey entre torres.
	bool all_ok = true;
	for (u16 seed = 0u; seed < 960u; ++seed) {
		PieceType back[8] {};
		if (!chess960_back_rank(seed, back)) {
			all_ok = false;
			break;
		}
		int light = -1;
		int dark = -1;
		int king = -1;
		int rook_lo = -1;
		int rook_hi = -1;
		for (int file = 0; file < 8; ++file) {
			if (back[file] == PieceType::Bishop) {
				if ((file & 1) == 0) {
					dark = file;
				} else {
					light = file;
				}
			}
			if (back[file] == PieceType::King) {
				king = file;
			}
			if (back[file] == PieceType::Rook) {
				if (rook_lo < 0) {
					rook_lo = file;
				}
				rook_hi = file;
			}
		}
		if (light < 0 || dark < 0 || king < 0 || rook_lo < 0 ||
		    !(rook_lo < king && king < rook_hi)) {
			all_ok = false;
			break;
		}
	}
	check(all_ok, "chess960: las 960 disposiciones son validas");
}

void test_chess960_start_deterministic() {
	const Position a = initial_position(ChessVariant::Chess960, 42u);
	const Position b = initial_position(ChessVariant::Chess960, 42u);
	check(a.key == b.key, "chess960: mismo indice -> misma posicion");
	check(king_square(a, Color::White) != kNoSquare, "chess960: hay rey blanco");
	check(a.castle_rook[0] != kNoSquare && a.castle_rook[1] != kNoSquare,
	      "chess960: torres de enroque resueltas");
}

void test_chess960_castling_arbitrary_files() {
	// Rey en b1 y torre en h1: enroque corto -> rey g1, torre f1.
	Position pos;
	pos.board[make_square(1u, 0u)] = make_piece(Color::White, PieceType::King);
	pos.board[make_square(7u, 0u)] = make_piece(Color::White, PieceType::Rook);
	pos.board[make_square(4u, 7u)] = make_piece(Color::Black, PieceType::King);
	pos.side = static_cast<u8>(Color::White);
	pos.castling = kCastleWhiteKing;
	rebuild_castle_rooks(pos);
	pos.key = compute_key(pos);

	MoveList legal;
	generate_legal(pos, legal);
	Move castle = kNoMove;
	for (eng::usize i = 0u; i < legal.size(); ++i) {
		if (move_is_castle_king(legal[i])) {
			castle = legal[i];
		}
	}
	check(!move_none(castle), "chess960: el enroque es legal con rey/torre desplazados");

	Undo undo;
	const eng::u32 key_before = pos.key;
	make_move(pos, castle, undo);
	check(pos.board[make_square(6u, 0u)] == make_piece(Color::White, PieceType::King),
	      "chess960: rey a g1");
	check(pos.board[make_square(5u, 0u)] == make_piece(Color::White, PieceType::Rook),
	      "chess960: torre a f1");
	check(pos.board[make_square(7u, 0u)] == kEmptyPiece, "chess960: h1 queda vacia");
	unmake_move(pos, castle, undo);
	check(pos.board[make_square(1u, 0u)] == make_piece(Color::White, PieceType::King) &&
	          pos.board[make_square(7u, 0u)] == make_piece(Color::White, PieceType::Rook) &&
	          pos.key == key_before,
	      "chess960: unmake restaura rey, torre y clave");
}

void test_chess960_castling_overlap() {
	// Rey en f1 y torre en g1: enroque corto -> rey g1, torre f1; rey y torre
	// INTERCAMBIAN casillas. Es el caso que rompia make/unmake (la torre se
	// perdia al escribir el rey en la casilla que ocupaba la torre).
	Position pos;
	pos.board[make_square(5u, 0u)] = make_piece(Color::White, PieceType::King);
	pos.board[make_square(6u, 0u)] = make_piece(Color::White, PieceType::Rook);
	pos.board[make_square(4u, 7u)] = make_piece(Color::Black, PieceType::King);
	pos.side = static_cast<u8>(Color::White);
	pos.castling = kCastleWhiteKing;
	rebuild_castle_rooks(pos);
	pos.key = compute_key(pos);

	MoveList legal;
	generate_legal(pos, legal);
	Move castle = kNoMove;
	for (eng::usize i = 0u; i < legal.size(); ++i) {
		if (move_is_castle_king(legal[i])) {
			castle = legal[i];
		}
	}
	check(!move_none(castle), "chess960 solape: el enroque rey/torre-intercambio es legal");

	Undo undo;
	const eng::u32 key_before = pos.key;
	make_move(pos, castle, undo);
	check(pos.board[make_square(6u, 0u)] == make_piece(Color::White, PieceType::King),
	      "chess960 solape: rey a g1");
	check(pos.board[make_square(5u, 0u)] == make_piece(Color::White, PieceType::Rook),
	      "chess960 solape: torre a f1 (no se pierde)");
	unmake_move(pos, castle, undo);
	check(pos.board[make_square(5u, 0u)] == make_piece(Color::White, PieceType::King) &&
	          pos.board[make_square(6u, 0u)] == make_piece(Color::White, PieceType::Rook) &&
	          pos.key == key_before,
	      "chess960 solape: unmake restaura rey, torre y clave");
}

void test_fast_tournament() {
	// Torneo rapido de 2 partidas con arranques Chess960 y profundidad 1.
	const ArenaResult result = arena_chess(2u, 1u, 3000u, ChessVariant::Chess960, 0u, 40u);
	check(result.games == 2u, "torneo: 2 partidas");
	check(result.white_wins + result.black_wins + result.draws + result.unfinished == 2u,
	      "torneo: marcador cuadra");

	// Una partida concreta desde la posicion estandar termina o avanza.
	Position pos;
	set_start(pos);
	ChessSearcher white;
	ChessSearcher black;
	const MatchOutcome game = play_game(white, black, pos, 1u, 3000u, 40u);
	check(game.plies > 0u, "torneo: la partida avanza");
}

} // namespace

int main() {
	std::printf("Ajedrez: variantes y torneos:\n");
	test_chess960_arrangements();
	test_chess960_start_deterministic();
	test_chess960_castling_arbitrary_files();
	test_chess960_castling_overlap();
	test_fast_tournament();

	if (g_fail == 0u) {
		std::printf("OK: Chess960 (960 disposiciones, enroque generalizado) y torneo rapido\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
