// ============================================================================
// Test HOST-138: nucleo de eng::board (tipos, Zobrist y concepto GameRules)
// ============================================================================
//
// Valida `engine/include/eng/board/core/{types,zobrist,game}.hpp`: geometria 0x88,
// empaquetado de pieza y jugada, puntuaciones, generador Zobrist determinista y el
// contrato `GameRules` (con una policy de prueba).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/138_board_core

#include <cstdio>

#include <eng/board/core/game.hpp>
#include <eng/board/core/types.hpp>
#include <eng/board/core/zobrist.hpp>

namespace {

using namespace eng::board;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct MockGame {
	using Position = Square;
	using Move = eng::board::Move;
	using MoveList = eng::board::MoveList;

	static Position initial() { return 0u; }
	static eng::u32 generate_legal(const Position&, MoveList&) { return 0u; }
	static bool in_check(const Position&) { return false; }
	static eng::u32 zobrist(const Position&) { return 0u; }
	static Terminal terminal(const Position&) { return Terminal::None; }
};

struct NotGame {};

static_assert(GameRules<MockGame>, "MockGame debe cumplir GameRules");
static_assert(!GameRules<NotGame>, "NotGame no debe cumplir GameRules");

void test_color_and_piece() {
	check(opposite(Color::White) == Color::Black, "color: opposite blanco");
	check(opposite(Color::Black) == Color::White, "color: opposite negro");

	const Piece white_knight = make_piece(Color::White, PieceType::Knight);
	const Piece black_rook = make_piece(Color::Black, PieceType::Rook);
	check(piece_type(white_knight) == PieceType::Knight, "pieza: tipo blanco");
	check(piece_color(white_knight) == Color::White, "pieza: color blanco");
	check(piece_type(black_rook) == PieceType::Rook, "pieza: tipo negro");
	check(piece_color(black_rook) == Color::Black, "pieza: color negro");
	check(piece_empty(kEmptyPiece) && !piece_empty(white_knight), "pieza: vacia");
}

void test_squares() {
	check(square_valid(make_square(0u, 0u)), "0x88: a1 valida");
	check(square_valid(make_square(7u, 7u)), "0x88: h8 valida");
	check(!square_valid(kNoSquare), "0x88: 0x80 invalida");
	check(square_file(make_square(3u, 5u)) == 3u, "0x88: file");
	check(square_rank(make_square(3u, 5u)) == 5u, "0x88: rank");
	check(compact_square(make_square(0u, 0u)) == 0u, "0x88: a1 compacta 0");
	check(compact_square(make_square(7u, 7u)) == 63u, "0x88: h8 compacta 63");
	// Suma fuera de tablero: la mascara 0x88 la detecta.
	const Square b1 = make_square(1u, 0u);
	check(!square_valid(static_cast<Square>(b1 + 0x07u)), "0x88: desborde de file");
}

void test_moves() {
	const Move move = make_move(make_square(4u, 1u), make_square(4u, 3u), 0x0002u);
	check(move_from(move) == make_square(4u, 1u), "move: from");
	check(move_to(move) == make_square(4u, 3u), "move: to");
	check(move_payload(move) == 0x0002u, "move: payload");
	check(!move_none(move), "move: no es nula");
	check(move_none(kNoMove), "move: sentinela");
}

void test_scores() {
	check(score_is_mate(kScoreMate), "score: mate positivo");
	check(score_is_mate(static_cast<Score>(-kScoreMate)), "score: mate negativo");
	check(!score_is_mate(static_cast<Score>(100)), "score: centipeones no es mate");
	check(terminal_is_over(Terminal::Checkmate) && !terminal_is_over(Terminal::None),
	      "terminal: is_over");
}

void test_zobrist() {
	eng::u32 a = 0x9e3779b9u;
	eng::u32 b = 0x9e3779b9u;
	bool same = true;
	for (int i = 0; i < 16; ++i) {
		if (zobrist_step(a) != zobrist_step(b)) {
			same = false;
		}
	}
	check(same, "zobrist: secuencia determinista");
	check(a != 0u, "zobrist: estado no nulo");

	ZobristKey key;
	check(key.value() == 0u, "zobrist key: arranca a 0");
	key.toggle(0x1234u);
	key.toggle(0x00ffu);
	const eng::u32 expected = 0x1234u ^ 0x00ffu;
	check(key.value() == expected, "zobrist key: acumula xor");
	key.toggle(0x1234u);
	key.toggle(0x00ffu);
	check(key.value() == 0u, "zobrist key: toggle es su propia inversa");
}

} // namespace

int main() {
	std::printf("eng::board core:\n");
	test_color_and_piece();
	test_squares();
	test_moves();
	test_scores();
	test_zobrist();

	if (g_fail == 0u) {
		std::printf("OK: eng::board core (tipos, 0x88, moves, score, zobrist, GameRules)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
