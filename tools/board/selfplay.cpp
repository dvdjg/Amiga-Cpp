// ============================================================================
// selfplay: partidas completas en host con la MISMA configuracion que la demo
// `demos/amiga/123_chess_match` (estilos agresivo/posicional, libro de aperturas,
// busqueda por rebanadas y relojes), sin UI, exportando cada partida a PGN.
// ============================================================================
//
// Reutiliza exactamente las mismas piezas que la demo:
//   - `StyledEval` + `g_active_weights` (eng/board/eval/styled_eval.hpp)
//   - libro de aperturas incorporado (eng/board/rules/chess/opening_book.hpp)
//   - `Searcher<ChessRules, StyledEval, ChessOrdering, 16384, false>`
//   - rebanadas de 64 nodos, hasta 8 frames por jugada, presupuesto derivado del
//     reloj (~1/25 restante), reloj 5:00 sin incremento, kFrameMs = 20.
//
// Uso:
//   selfplay [games] [--variant standard|chess960] [--seed N] [--max-plies N]
//            [--out ruta.pgn] [--swap] [--no-book] [--quiet]
//   por defecto: 1 standard 0 300 out/board/selfplay/selfplay.pgn

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <eng/board/eval/styled_eval.hpp>
#include <eng/board/rules/chess/fen.hpp>
#include <eng/board/rules/chess/opening_book.hpp>
#include <eng/board/rules/chess/pgn.hpp>
#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/rules/chess/variant.hpp>
#include <eng/board/search/search.hpp>

using namespace eng::board;
using namespace eng::board::chess;

using eng::s32;
using eng::u16;
using eng::u32;
using eng::u64;
using eng::usize;

namespace {

// --- Configuracion identica a demos/amiga/123_chess_match -------------------
constexpr eng::u32 kTtEntries = 16384u;
constexpr eng::u16 kMaxDepth = 12u;
constexpr eng::u64 kSliceNodes = 200u;
constexpr eng::s32 kStartMs = 300000; // 5:00
constexpr eng::s32 kIncrementMs = 0;
constexpr eng::u32 kMoveThinkFrames = 20u;
constexpr eng::u32 kMinThinkFrames = 3u;
constexpr eng::s32 kFrameMs = 20;
constexpr eng::usize kBookMax = 64u;
constexpr eng::usize kPgnCap = 1u << 16; // 64 KB por partida

using Engine = Searcher<ChessRules, StyledEval, ChessOrdering, kTtEntries, false>;

BookEntry g_book[kBookMax] {};
eng::u32 g_book_count = 0u;
char g_pgn[kPgnCap];

// Presupuesto de pensamiento (por defecto, el de la demo; ajustable para analisis).
eng::u64 g_slice_nodes = kSliceNodes;
eng::u32 g_think_frames = kMoveThinkFrames;
eng::u32 g_min_frames = kMinThinkFrames;

// Dos motores reutilizados entre partidas (TT de 192 KB cada uno; en estatica).
Engine g_engine_a {};
Engine g_engine_b {};

[[nodiscard]] eng::u32 move_budget_frames(eng::s32 clock_ms) noexcept {
	eng::s32 budget_ms = clock_ms / 25;
	const eng::s32 cap_ms = static_cast<eng::s32>(g_think_frames) * kFrameMs;
	if (budget_ms > cap_ms) {
		budget_ms = cap_ms;
	}
	eng::s32 frames = budget_ms / kFrameMs;
	if (frames < static_cast<eng::s32>(g_min_frames)) {
		frames = static_cast<eng::s32>(g_min_frames);
	}
	return static_cast<eng::u32>(frames);
}

void format_score(char* dst, eng::usize cap, Score score) noexcept {
	const bool negative = score < 0;
	const int magnitude = negative ? -static_cast<int>(score) : static_cast<int>(score);
	std::snprintf(dst, cap, "%s%d.%02d", negative ? "-" : "+", magnitude / 100,
	              magnitude % 100);
}

struct GameConfig {
	const char* white_name = "Agresivo";
	const char* black_name = "Posicional";
	EvalWeights white_style = aggressive_weights();
	EvalWeights black_style = positional_weights();
	bool use_book = true;
};

struct GameStats {
	eng::u32 plies = 0u;
	eng::u64 nodes = 0u;
};

/// Una jugada registrada para volcar el PGN al final, cuando ya se conoce el
/// resultado (que debe ir en la cabecera `[Result]`).
struct MoveRecord {
	eng::u16 fullmove = 0u;
	eng::u8 side = 0u; // 0 blancas, 1 negras
	char san[12] {};
	char comment[48] {};
};

constexpr eng::usize kMaxRecords = 512u;
MoveRecord g_records[kMaxRecords];

/// Juega una partida completa desde `pos` y escribe su PGN en `g_pgn`.
/// Devuelve la longitud del PGN. `result` recibe "1-0"/"0-1"/"1/2-1/2"/"*".
eng::usize play_game(Position pos, const GameConfig& cfg, const char* round, u32 max_plies,
                     ChessVariant variant, char* result, eng::usize result_cap, GameStats& stats) {
	g_engine_a.clear();
	g_engine_b.clear();

	result[0] = '*';
	result[1] = '\0';

	char fen[128];
	fen[0] = '\0';
	if (variant != ChessVariant::Standard) {
		(void)to_fen(pos, eng::Span<char> {fen, sizeof(fen)});
	}

	// Relojes.
	eng::s32 clock_a = kStartMs;
	eng::s32 clock_b = kStartMs;
	const char* end_note = nullptr;
	eng::usize records = 0u;

	for (eng::u32 ply = 0u; ply < max_plies; ++ply) {
		const Terminal terminal_now = terminal(pos);
		if (terminal_now == Terminal::Checkmate) {
			std::snprintf(result, result_cap, "%s",
			              (to_move(pos) == Color::White) ? "0-1" : "1-0");
			break;
		}
		if (terminal_now != Terminal::None) {
			std::snprintf(result, result_cap, "1/2-1/2");
			break;
		}

		const Color side = to_move(pos);
		const bool white_side = (side == Color::White);
		Engine& engine = white_side ? g_engine_a : g_engine_b;
		const EvalWeights style = white_side ? cfg.white_style : cfg.black_style;
		eng::s32& clock = white_side ? clock_a : clock_b;
		const eng::u16 fullmove = pos.fullmove;

		Move move = kNoMove;
		bool from_book = false;
		if (cfg.use_book) {
			const BookProbe probe =
			    probe_opening_book(pos, eng::Span<const BookEntry> {g_book, g_book_count});
			if (probe.found) {
				move = probe.move;
				from_book = true;
			}
		}

		u32 depth = 0u;
		Score score = 0;
		eng::u64 move_nodes = 0u;
		if (!from_book) {
			g_active_weights = style;
			const eng::u32 budget = move_budget_frames(clock);
			for (eng::u32 slice = 0u; slice < budget; ++slice) {
				const Engine::Result r = engine.search(pos, {kMaxDepth, g_slice_nodes});
				if (!move_none(r.best_move)) {
					move = r.best_move;
					if (r.depth > 0u) {
						depth = r.depth;
						score = r.score;
					}
				}
				move_nodes += r.nodes;
			}
			clock -= static_cast<eng::s32>(budget) * kFrameMs;
		}
		clock += kIncrementMs;

		if (move_none(move)) {
			std::snprintf(result, result_cap, "1/2-1/2");
			break;
		}

		// SAN antes de aplicar la jugada (usa la posición previa).
		char san[12];
		(void)to_san(pos, move, eng::Span<char> {san, sizeof(san)});

		Undo undo;
		make_move(pos, move, undo);

		if (records < kMaxRecords) {
			MoveRecord& rec = g_records[records++];
			rec.fullmove = fullmove;
			rec.side = static_cast<eng::u8>((side == Color::White) ? 0u : 1u);
			eng::usize i = 0u;
			for (; san[i] != '\0' && i + 1u < sizeof(rec.san); ++i) {
				rec.san[i] = san[i];
			}
			rec.san[i] = '\0';
			if (from_book) {
				std::snprintf(rec.comment, sizeof(rec.comment), "libro");
			} else {
				char score_text[16];
				format_score(score_text, sizeof(score_text), score);
				std::snprintf(rec.comment, sizeof(rec.comment), "d%lu %s n%llu",
				              static_cast<unsigned long>(depth), score_text,
				              static_cast<unsigned long long>(move_nodes));
			}
		}

		++stats.plies;
		stats.nodes += move_nodes;

		if (clock <= 0) {
			std::snprintf(result, result_cap, "%s", white_side ? "0-1" : "1-0");
			end_note = "tiempo agotado";
			break;
		}
	}

	// Causas de fin no cubiertas por la posicion final.
	if (result[0] == '*' && end_note == nullptr) {
		end_note = "limite de jugadas";
	}

	// Volcado del PGN, ya con el resultado final.
	PgnWriter pgn {eng::Span<char> {g_pgn, kPgnCap}};
	pgn.tag("Event", "123_chess_match selfplay");
	pgn.tag("Site", "host");
	pgn.tag("Date", "????.??.??");
	pgn.tag("Round", round);
	pgn.tag("White", cfg.white_name);
	pgn.tag("Black", cfg.black_name);
	pgn.tag("Result", result);
	if (variant != ChessVariant::Standard) {
		pgn.tag("Variant", "Chess960");
		pgn.tag("SetUp", "1");
		pgn.tag("FEN", fen);
	}
	pgn.end_tags();
	for (eng::usize i = 0u; i < records; ++i) {
		const MoveRecord& rec = g_records[i];
		const Color side = (rec.side == 0u) ? Color::White : Color::Black;
		pgn.move(rec.fullmove, side, eng::util::StringView {rec.san});
		if (rec.comment[0] != '\0') {
			pgn.comment(eng::util::StringView {rec.comment});
		}
	}
	if (end_note != nullptr) {
		pgn.comment(eng::util::StringView {end_note});
	}
	pgn.result(eng::util::StringView {result});
	if (!pgn.ok()) {
		std::fprintf(stderr, "selfplay: aviso: PGN truncado (%u bytes)\n",
		             static_cast<unsigned>(pgn.length()));
	}
	return pgn.length();
}

} // namespace

int main(int argc, char** argv) {
	eng::u32 games = 1u;
	u32 max_plies = 300u;
	u16 seed_base = 0u;
	bool swap = false;
	bool use_book = true;
	bool quiet = false;
	ChessVariant variant = ChessVariant::Standard;
	const char* out_path = "out/board/selfplay/selfplay.pgn";

	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (std::strcmp(a, "--variant") == 0 && i + 1 < argc) {
			variant = (argv[++i][0] == 'c') ? ChessVariant::Chess960 : ChessVariant::Standard;
		} else if (std::strcmp(a, "--seed") == 0 && i + 1 < argc) {
			seed_base = static_cast<u16>(std::atoi(argv[++i]));
		} else if (std::strcmp(a, "--max-plies") == 0 && i + 1 < argc) {
			max_plies = static_cast<u32>(std::atoi(argv[++i]));
		} else if (std::strcmp(a, "--out") == 0 && i + 1 < argc) {
			out_path = argv[++i];
		} else if (std::strcmp(a, "--slice-nodes") == 0 && i + 1 < argc) {
			g_slice_nodes = static_cast<eng::u64>(std::strtoull(argv[++i], nullptr, 10));
		} else if (std::strcmp(a, "--frames") == 0 && i + 1 < argc) {
			g_think_frames = static_cast<u32>(std::atoi(argv[++i]));
		} else if (std::strcmp(a, "--min-frames") == 0 && i + 1 < argc) {
			g_min_frames = static_cast<u32>(std::atoi(argv[++i]));
		} else if (std::strcmp(a, "--swap") == 0) {
			swap = true;
		} else if (std::strcmp(a, "--no-book") == 0) {
			use_book = false;
		} else if (std::strcmp(a, "--quiet") == 0) {
			quiet = true;
		} else if (a[0] != '-') {
			games = static_cast<eng::u32>(std::atoi(a));
		}
	}
	if (games == 0u) {
		games = 1u;
	}

	g_book_count = build_opening_book(eng::Span<BookEntry> {g_book, kBookMax});

	const std::filesystem::path out_p{out_path};
	if (out_p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(out_p.parent_path(), ec);
	}
	std::FILE* file = std::fopen(out_path, "wb");
	if (file == nullptr) {
		std::fprintf(stderr, "selfplay: no se pudo abrir '%s' para escritura\n", out_path);
		return 1;
	}

	eng::u32 white_wins = 0u;
	eng::u32 black_wins = 0u;
	eng::u32 draws = 0u;
	eng::u32 unfinished = 0u;
	eng::u64 total_plies = 0u;
	eng::u64 total_nodes = 0u;

	for (eng::u32 g = 0u; g < games; ++g) {
		const u16 seed = static_cast<u16>(seed_base + static_cast<u16>(g));
		Position pos = initial_position(variant, seed);

		GameConfig cfg;
		if (swap && (g & 1u) != 0u) {
			cfg.white_name = "Posicional";
			cfg.black_name = "Agresivo";
			cfg.white_style = positional_weights();
			cfg.black_style = aggressive_weights();
		}
		cfg.use_book = use_book;

		char round[16];
		std::snprintf(round, sizeof(round), "%u", static_cast<unsigned>(g + 1u));
		char result[8];
		GameStats stats;
		const eng::usize len = play_game(pos, cfg, round, max_plies, variant, result,
		                                 sizeof(result), stats);

		(void)std::fwrite(g_pgn, 1u, len, file);
		(void)std::fwrite("\n", 1u, 1u, file);

		if (std::strcmp(result, "1-0") == 0) {
			++white_wins;
		} else if (std::strcmp(result, "0-1") == 0) {
			++black_wins;
		} else if (std::strcmp(result, "1/2-1/2") == 0) {
			++draws;
		} else {
			++unfinished;
		}
		total_plies += stats.plies;
		total_nodes += stats.nodes;

		if (!quiet) {
			std::printf("game %u/%u  %s-%s  %s  %u plies  %llu nodos\n",
			            static_cast<unsigned>(g + 1u), static_cast<unsigned>(games),
			            cfg.white_name, cfg.black_name, result,
			            static_cast<unsigned>(stats.plies),
			            static_cast<unsigned long long>(stats.nodes));
		}
	}
	std::fclose(file);

	std::printf("selfplay: %u partidas (%s), blancas %u | negras %u | tablas %u | sin acabar %u\n",
	            static_cast<unsigned>(games), variant_name(variant),
	            static_cast<unsigned>(white_wins), static_cast<unsigned>(black_wins),
	            static_cast<unsigned>(draws), static_cast<unsigned>(unfinished));
	if (games > 0u) {
		std::printf("  media %.1f plies | %.0f nodos/partida\n",
		            static_cast<double>(total_plies) / static_cast<double>(games),
		            static_cast<double>(total_nodes) / static_cast<double>(games));
	}
	std::printf("  PGN: %s\n", out_path);
	return 0;
}
