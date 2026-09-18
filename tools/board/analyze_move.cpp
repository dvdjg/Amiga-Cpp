// ============================================================================
// analyze_move: reanaliza una posicion y explica la decision del motor.
// ============================================================================
//
// Toma una posicion (FEN directo o una linea de un volcado de selfplay) y lista las
// mejores jugadas de la raiz con su puntuacion exacta, evaluacion estatica y linea
// principal, usando la MISMA configuracion de estilo que la demo (pesos agresivo o
// posicional). Sirve para "capturar una jugada en cualquier momento" y volver a
// entender por que se eligio.
//
// Uso:
//   analyze_move --fen "rnbq... w KQkq - 0 1" [--style aggressive|positional]
//                [--depth D] [--multi K]
//   analyze_move --dump out/board/selfplay/pos.txt --ply N [--depth D] [--multi K]
//
// El volcado lo genera `selfplay --dump-positions <file>`.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include <eng/board/eval/styled_eval.hpp>
#include <eng/board/rules/chess/fen.hpp>
#include <eng/board/rules/chess/notation.hpp>
#include <eng/board/rules/chess/rules.hpp>
#include <eng/board/search/search.hpp>

using namespace eng::board;
using namespace eng::board::chess;

using eng::s32;
using eng::u16;
using eng::u32;
using eng::u64;
using eng::usize;

namespace {

constexpr eng::u32 kTtEntries = 16384u;
constexpr eng::u32 kMultiMax = 32u;
using Engine = Searcher<ChessRules, StyledEval, ChessOrdering, kTtEntries, false>;

void format_score(char* dst, usize cap, Score score) {
	const bool negative = score < 0;
	const int magnitude = negative ? -static_cast<int>(score) : static_cast<int>(score);
	std::snprintf(dst, cap, "%s%d.%02d", negative ? "-" : "+", magnitude / 100, magnitude % 100);
}

void print_pv(const Position& root, const Engine::Line& line) {
	Position work = root;
	for (u32 i = 0u; i < line.length; ++i) {
		const Move move = line.pv[i];
		char san[12];
		(void)to_san(work, move, eng::Span<char> {san, sizeof(san)});
		std::printf(" %s", san);
		Undo undo;
		make_move(work, move, undo);
	}
}

/// Extrae (ply, side, fen, uci, san, depth, score, nodes) de una linea del volcado.
bool parse_dump_line(const std::string& line, u32 wanted_ply, std::string& fen_out,
                     std::string& uci_out, std::string& san_out) {
	std::istringstream iss(line);
	std::vector<std::string> tok;
	std::string t;
	while (iss >> t) {
		tok.push_back(t);
	}
	// ply side f1 f2 f3 f4 f5 f6 | uci san ...
	if (tok.size() < 11u || tok[8] != "|") {
		return false;
	}
	if (static_cast<u32>(std::atoi(tok[0].c_str())) != wanted_ply) {
		return false;
	}
	fen_out = tok[2] + " " + tok[3] + " " + tok[4] + " " + tok[5] + " " + tok[6] + " " + tok[7];
	uci_out = tok[9];
	san_out = tok[10];
	return true;
}

} // namespace

int main(int argc, char** argv) {
	const char* fen_arg = nullptr;
	const char* dump_path = nullptr;
	u32 ply = 0u;
	bool have_ply = false;
	u32 depth = 6u;
	u32 multi = 8u;
	bool positional = false;

	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (std::strcmp(a, "--fen") == 0 && i + 1 < argc) {
			fen_arg = argv[++i];
		} else if (std::strcmp(a, "--dump") == 0 && i + 1 < argc) {
			dump_path = argv[++i];
		} else if (std::strcmp(a, "--ply") == 0 && i + 1 < argc) {
			ply = static_cast<u32>(std::atoi(argv[++i]));
			have_ply = true;
		} else if (std::strcmp(a, "--depth") == 0 && i + 1 < argc) {
			depth = static_cast<u32>(std::atoi(argv[++i]));
		} else if (std::strcmp(a, "--multi") == 0 && i + 1 < argc) {
			multi = static_cast<u32>(std::atoi(argv[++i]));
		} else if (std::strcmp(a, "--style") == 0 && i + 1 < argc) {
			positional = (argv[++i][0] == 'p');
		}
	}
	if (multi == 0u) {
		multi = 1u;
	}
	if (multi > kMultiMax) {
		multi = kMultiMax;
	}

	std::string fen;
	std::string chosen_uci;
	std::string chosen_san;
	if (dump_path != nullptr) {
		if (!have_ply) {
			std::fprintf(stderr, "analyze_move: --dump requiere --ply N\n");
			return 2;
		}
		std::FILE* f = std::fopen(dump_path, "rb");
		if (f == nullptr) {
			std::fprintf(stderr, "analyze_move: no se pudo abrir '%s'\n", dump_path);
			return 2;
		}
		char buf[512];
		bool found = false;
		while (std::fgets(buf, sizeof(buf), f) != nullptr) {
			if (buf[0] == '#') {
				continue;
			}
			if (parse_dump_line(buf, ply, fen, chosen_uci, chosen_san)) {
				found = true;
				break;
			}
		}
		std::fclose(f);
		if (!found) {
			std::fprintf(stderr, "analyze_move: no se encontro el ply %lu en '%s'\n",
			             static_cast<unsigned long>(ply), dump_path);
			return 2;
		}
	} else if (fen_arg != nullptr) {
		fen = fen_arg;
	} else {
		std::fprintf(stderr,
		             "uso: analyze_move --fen \"...\" | --dump <pos.txt> --ply N "
		             "[--style aggressive|positional] [--depth D] [--multi K]\n");
		return 2;
	}

	Position pos;
	if (!set_from_fen(pos, eng::util::StringView {fen.c_str(), fen.size()})) {
		std::fprintf(stderr, "analyze_move: FEN invalido: %s\n", fen.c_str());
		return 2;
	}

	g_active_weights = positional ? positional_weights() : aggressive_weights();
	const char* style_name = positional ? "posicional" : "agresivo";
	const Color stm = to_move(pos);
	std::printf("posicion : %s\n", fen.c_str());
	std::printf("turno    : %s | estilo: %s | profundidad: %lu | lineas: %lu\n",
	            (stm == Color::White) ? "blancas" : "negras", style_name,
	            static_cast<unsigned long>(depth), static_cast<unsigned long>(multi));
	if (!chosen_uci.empty()) {
		std::printf("jugada del volcado: %s (%s) en el ply %lu\n", chosen_uci.c_str(),
		            chosen_san.c_str(), static_cast<unsigned long>(ply));
	}

	Engine engine;
	Engine::Line lines[kMultiMax] {};
	const u32 produced =
	    engine.search_multi_pv(pos, {depth, 0u}, multi, eng::Span<Engine::Line> {lines, multi});

	std::printf("\n%-4s %-8s %8s %8s  %s\n", "#", "jugada", "busqueda", "estatica", "linea principal");
	for (u32 i = 0u; i < produced; ++i) {
		const Engine::Line& line = lines[i];
		char san[12];
		(void)to_san(pos, line.move, eng::Span<char> {san, sizeof(san)});
		char uci[8];
		(void)to_uci(line.move, eng::Span<char> {uci, sizeof(uci)});

		Position after = pos;
		Undo undo;
		make_move(after, line.move, undo);
		g_active_weights = positional ? positional_weights() : aggressive_weights();
		const Score static_eval = evaluate_styled(after, g_active_weights);

		char search_text[16];
		char static_text[16];
		format_score(search_text, sizeof(search_text), line.score);
		format_score(static_text, sizeof(static_text), static_eval);

		const bool is_chosen = !chosen_uci.empty() && chosen_uci == uci;
		std::printf("%-4lu %-8s %8s %8s  %s", static_cast<unsigned long>(i + 1u), san,
		            search_text, static_text, is_chosen ? "<== elegida" : "");
		print_pv(pos, line);
		std::printf("\n");
	}
	return 0;
}
