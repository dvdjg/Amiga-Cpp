// ============================================================================
// Test HOST-140: reglas de ajedrez 0x88, generacion legal y perft
// ============================================================================
//
// Valida `engine/include/eng/board/rules/chess/{board,movegen,fen,rules}.hpp`:
// FEN, make/unmake con Zobrist incremental, generacion legal (perft contra valores
// conocidos) y deteccion de fin de partida.
//
// perft cubre enroque, al paso y promocion (posicion inicial, Kiwipete, posicion de
// al paso de CPW y una de promociones).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/140_chess_movegen

#include <cstdio>
#include <cstring>

#include <eng/board/rules/chess/board.hpp>
#include <eng/board/rules/chess/fen.hpp>
#include <eng/board/rules/chess/movegen.hpp>
#include <eng/board/rules/chess/rules.hpp>

namespace {

using namespace eng::board;
using namespace eng::board::chess;
using eng::u32;
using eng::u64;
using eng::usize;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

Position position_from(const char* fen) {
	Position pos;
	const bool ok = set_from_fen(pos, fen);
	check(ok, "fen: se lee la posicion");
	return pos;
}

/// Escribe la posición y devuelve la vista FEN del buffer (sin `strcmp` ni punteros).
eng::util::StringView fen_text(const Position& pos, char* buffer, eng::usize cap) {
	const eng::usize n = to_fen(pos, eng::Span<char> {buffer, cap});
	return eng::util::StringView(buffer, n);
}

void test_fen_roundtrip() {
	const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
	Position pos = position_from(start);
	char out[128];
	check(fen_text(pos, out, sizeof(out)) == eng::util::StringView(start),
	      "fen: round-trip de la posicion inicial");

	Position kiwipete = position_from("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
	char out2[128];
	check(fen_text(kiwipete, out2, sizeof(out2)) ==
	          eng::util::StringView(
	              "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"),
	      "fen: round-trip de Kiwipete");
}

void test_make_unmake_restores() {
	Position pos = position_from("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
	char before_buffer[128];
	const eng::util::StringView before = fen_text(pos, before_buffer, sizeof(before_buffer));
	MoveList legal;
	generate_legal(pos, legal);
	bool restored = true;
	for (eng::usize i = 0; i < legal.size(); ++i) {
		Undo undo;
		make_move(pos, legal[i], undo);
		unmake_move(pos, legal[i], undo);
		char after_buffer[128];
		if (fen_text(pos, after_buffer, sizeof(after_buffer)) != before) {
			restored = false;
		}
	}
	check(restored, "make/unmake: restaura tablero, estado y clave");
}

void verify_keys(Position& pos, u32 depth) {
	if (pos.key != compute_key(pos)) {
		check(false, "zobrist: incremental coincide con recompute");
		return;
	}
	if (depth == 0u) {
		return;
	}
	MoveList legal;
	generate_legal(pos, legal);
	for (eng::usize i = 0; i < legal.size(); ++i) {
		Undo undo;
		make_move(pos, legal[i], undo);
		verify_keys(pos, depth - 1u);
		unmake_move(pos, legal[i], undo);
	}
}

void test_zobrist_consistency() {
	Position pos = position_from("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8");
	verify_keys(pos, 3u);
}

void test_perft(const char* fen, const eng::u64* expected, u32 max_depth) {
	Position pos = position_from(fen);
	for (u32 depth = 1u; depth <= max_depth; ++depth) {
		const eng::u64 nodes = perft(pos, depth);
		if (nodes != expected[depth - 1u]) {
			std::printf("[FAIL] perft d%u: %llu (esperado %llu)\n", static_cast<unsigned>(depth),
			            static_cast<unsigned long long>(nodes),
			            static_cast<unsigned long long>(expected[depth - 1u]));
			++g_fail;
		}
	}
}

void test_perft_all() {
	const eng::u64 start[] = {20u, 400u, 8902u, 197281u};
	test_perft("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", start, 4u);

	const eng::u64 kiwipete[] = {48u, 2039u, 97862u};
	test_perft("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", kiwipete, 3u);

	const eng::u64 en_passant[] = {14u, 191u, 2812u, 43238u};
	test_perft("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", en_passant, 4u);

	const eng::u64 promotions[] = {44u, 1486u, 62379u};
	test_perft("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", promotions, 3u);
}

void test_terminal() {
	// Mate del loco.
	Position mate = position_from("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
	check(terminal(mate) == Terminal::Checkmate, "terminal: jaque mate");

	// Rey ahogado.
	Position stale = position_from("k7/8/1Q6/8/8/8/8/7K b - - 0 1");
	check(terminal(stale) == Terminal::Stalemate, "terminal: rey ahogado");

	// Material insuficiente.
	Position bare = position_from("8/8/4k3/8/8/4K3/8/8 w - - 0 1");
	check(terminal(bare) == Terminal::InsufficientMaterial, "terminal: K vs K");
	Position rook = position_from("8/8/4k3/8/8/4K3/8/7R w - - 0 1");
	check(!terminal_is_over(terminal(rook)), "terminal: K+R vs K no es tablas");
}

void test_game_rules_policy() {
	static_assert(GameRules<ChessRules>, "ChessRules cumple el contrato");
	const ChessRules::Position start = ChessRules::initial();
	ChessRules::MoveList legal;
	const u32 count = ChessRules::generate_legal(start, legal);
	check(count == 20u, "ChessRules: 20 jugadas iniciales");
	check(ChessRules::zobrist(start) == compute_key(start), "ChessRules: clave coherente");
	check(!ChessRules::in_check(start), "ChessRules: la posicion inicial no es jaque");
}

} // namespace

int main() {
	std::printf("Ajedrez 0x88:\n");
	test_fen_roundtrip();
	test_make_unmake_restores();
	test_zobrist_consistency();
	test_perft_all();
	test_terminal();
	test_game_rules_policy();

	if (g_fail == 0u) {
		std::printf("OK: ajedrez 0x88 (fen, make/unmake, zobrist, perft, terminal, GameRules)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
