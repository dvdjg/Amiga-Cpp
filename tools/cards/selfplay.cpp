// ============================================================================
// cards selfplay: partidas de poker completas CPU vs CPU en host para ajustar el
// nivel (estilos, perfiles de memoria, tabla preflop y rango de rival), sin UI ni
// emulador. Mide net por asiento, bb/100, showdowns y actividad.
// ============================================================================
//
// Reutiliza las mismas piezas que el juego consumira:
//   - `eng::cards::run_session` (sim/session.hpp) con boton rotando y recompra.
//   - estilos `BotStyle` (ai/bot.hpp) ortogonales al footprint.
//   - perfiles `CardPlan` N20..N512 (core/budget.hpp).
//   - tabla preflop de 169 clases y rango de rival (eval/range.hpp).
//
// Uso:
//   selfplay.sh [hands] [--seats N] [--seed S] [--stack N] [--sb N] [--bb N]
//               [--profile N20|N64|N128|N256|N512] [--sessions N]
//               [--table-samples N] [--range-classes N] [--styles tp,ta,lp,la,eq]
//               [--no-range] [--out ruta.txt] [--quiet]
//   por defecto: 200 --seats 6 --seed 1 --profile N512
//
// Salida: informe por asiento (estilo, net, bb/100) y totales. Con `--out` se escribe
// a `out/cards/selfplay/` (por defecto).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include <eng/cards/core/budget.hpp>
#include <eng/cards/eval/range.hpp>
#include <eng/cards/sim/session.hpp>

using namespace eng::cards;

using eng::s32;
using eng::u8;
using eng::u16;
using eng::u32;

namespace {

constexpr u8 kSeats = kMaxSeats;

struct Cli {
	u16 hands = 200u;
	u8 seats = 6u;
	u32 seed = 1u;
	s32 stack = 1000;
	s32 small_blind = 5;
	s32 big_blind = 10;
	CardProfile profile = CardProfile::N512;
	u16 sessions = 1u;
	u16 table_samples = 64u;
	u8 range_classes = 40u;
	bool use_range = true;
	bool quiet = false;
	std::string out = "out/cards/selfplay/selfplay.txt";
	BotStyle styles[kSeats] {};
	u8 style_count = 0u;
};

[[nodiscard]] bool parse_u32(const char* text, u32& value) {
	if (text == nullptr || *text == '\0') {
		return false;
	}
	char* end = nullptr;
	const unsigned long v = std::strtoul(text, &end, 10);
	if (end == nullptr || *end != '\0') {
		return false;
	}
	value = static_cast<u32>(v);
	return true;
}

[[nodiscard]] BotStyle parse_style(const std::string& name) {
	if (name == "tp") {
		return BotStyle::TightPassive;
	}
	if (name == "ta") {
		return BotStyle::TightAggressive;
	}
	if (name == "lp") {
		return BotStyle::LoosePassive;
	}
	if (name == "la") {
		return BotStyle::LooseAggressive;
	}
	return BotStyle::Balanced;
}

[[nodiscard]] const char* style_name(BotStyle style) {
	switch (style) {
	case BotStyle::TightPassive:
		return "tight-pasivo";
	case BotStyle::TightAggressive:
		return "tight-agresivo";
	case BotStyle::LoosePassive:
		return "loose-pasivo";
	case BotStyle::LooseAggressive:
		return "loose-agresivo";
	case BotStyle::Balanced:
		return "equilibrado";
	case BotStyle::Count:
		break;
	}
	return "?";
}

[[nodiscard]] bool parse_profile(const std::string& name, CardProfile& profile) {
	if (name == "N20") {
		profile = CardProfile::N20;
	} else if (name == "N64") {
		profile = CardProfile::N64;
	} else if (name == "N128") {
		profile = CardProfile::N128;
	} else if (name == "N256") {
		profile = CardProfile::N256;
	} else if (name == "N512") {
		profile = CardProfile::N512;
	} else {
		return false;
	}
	return true;
}

[[nodiscard]] const char* profile_name(CardProfile profile) {
	switch (profile) {
	case CardProfile::N20:
		return "N20";
	case CardProfile::N64:
		return "N64";
	case CardProfile::N128:
		return "N128";
	case CardProfile::N256:
		return "N256";
	case CardProfile::N512:
		return "N512";
	case CardProfile::Count:
		break;
	}
	return "?";
}

/// Estilos por defecto: se rotan para que la mesa enfrente estilos distintos.
void assign_default_styles(SessionConfig& cfg) {
	constexpr BotStyle kOrder[5] = {BotStyle::TightAggressive, BotStyle::LoosePassive,
	                                BotStyle::Balanced, BotStyle::LooseAggressive,
	                                BotStyle::TightPassive};
	for (u8 i = 0u; i < kSeats; ++i) {
		cfg.styles[i] = kOrder[i % 5u];
	}
}

} // namespace

int main(int argc, char** argv) {
	Cli cli {};

	// Primer argumento posicional: numero de manos.
	if (argc > 1 && argv[1][0] != '-') {
		u32 v = 0u;
		if (parse_u32(argv[1], v)) {
			cli.hands = static_cast<u16>(v);
		}
	}

	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		auto next = [&](const char* name) -> const char* {
			if (i + 1 < argc) {
				return argv[++i];
			}
			std::fprintf(stderr, "falta valor para %s\n", name);
			std::exit(2);
		};
		u32 v = 0u;
		if (arg == "--seats" && parse_u32(next("--seats"), v)) {
			cli.seats = static_cast<u8>(v < 2u ? 2u : (v > kSeats ? kSeats : v));
		} else if (arg == "--seed" && parse_u32(next("--seed"), v)) {
			cli.seed = v;
		} else if (arg == "--stack" && parse_u32(next("--stack"), v)) {
			cli.stack = static_cast<s32>(v);
		} else if (arg == "--sb" && parse_u32(next("--sb"), v)) {
			cli.small_blind = static_cast<s32>(v);
		} else if (arg == "--bb" && parse_u32(next("--bb"), v)) {
			cli.big_blind = static_cast<s32>(v);
		} else if (arg == "--sessions" && parse_u32(next("--sessions"), v)) {
			cli.sessions = static_cast<u16>(v == 0u ? 1u : v);
		} else if (arg == "--table-samples" && parse_u32(next("--table-samples"), v)) {
			cli.table_samples = static_cast<u16>(v);
		} else if (arg == "--range-classes" && parse_u32(next("--range-classes"), v)) {
			cli.range_classes = static_cast<u8>(v > kPreflopClasses ? kPreflopClasses : v);
		} else if (arg == "--profile") {
			if (!parse_profile(next("--profile"), cli.profile)) {
				std::fprintf(stderr, "perfil desconocido; usa N20|N64|N128|N256|N512\n");
				return 2;
			}
		} else if (arg == "--no-range") {
			cli.use_range = false;
		} else if (arg == "--styles") {
			const char* text = next("--styles");
			std::string token;
			for (const char* p = text;; ++p) {
				if (*p == ',' || *p == '\0') {
					if (!token.empty() && cli.style_count < kSeats) {
						cli.styles[cli.style_count++] = parse_style(token);
					}
					token.clear();
					if (*p == '\0') {
						break;
					}
				} else {
					token += *p;
				}
			}
		} else if (arg == "--out") {
			cli.out = next("--out");
		} else if (arg == "--quiet") {
			cli.quiet = true;
		}
	}

	const CardPlan plan = card_profile_plan(cli.profile);

	SessionConfig cfg {};
	cfg.seats = cli.seats;
	cfg.starting_stack = cli.stack;
	cfg.small_blind = cli.small_blind;
	cfg.big_blind = cli.big_blind;
	cfg.hands = cli.hands;
	cfg.seed = cli.seed;
	assign_default_styles(cfg);
	for (u8 i = 0u; i < cli.style_count && i < cli.seats; ++i) {
		cfg.styles[i] = cli.styles[i];
	}

	// Tabla preflop (una vez) y rango de rival derivado de ella.
	eng::Xoroshiro64pp table_rng {cli.seed ^ 0x51ed2701u, cli.seed + 0x9e3779b9u};
	PreflopTable table;
	HandRange opponent_range;
	build_preflop_table(table, table_rng, cli.table_samples);
	if (cli.use_range) {
		make_range_by_equity(table, opponent_range, cli.range_classes);
	}

	SessionStats total {};
	total.starting_stack = cli.stack;
	for (u16 session = 0u; session < cli.sessions; ++session) {
		SessionConfig run_cfg = cfg;
		run_cfg.seed = cli.seed + static_cast<u32>(session);
		SessionStats stats {};
		run_session(run_cfg, plan, stats, nullptr, table.ready ? &table : nullptr,
		            cli.use_range ? &opponent_range : nullptr);
		for (u8 i = 0u; i < cli.seats; ++i) {
			total.net[i] += stats.net[i];
		}
		total.hands_played += stats.hands_played;
		total.showdowns += stats.showdowns;
		total.fold_wins += stats.fold_wins;
		total.raises += stats.raises;
		total.calls += stats.calls;
		total.folds += stats.folds;
		if (stats.max_actions_in_hand > total.max_actions_in_hand) {
			total.max_actions_in_hand = stats.max_actions_in_hand;
		}
	}

	std::string report;
	report.reserve(2048);
	char line[256];
	auto put = [&](const char* text) { report += text; };
	std::snprintf(line, sizeof(line),
	              "== cards selfplay ==\nperfil=%s footprint_plan=%u kB seats=%u hands=%u sessions=%u "
	              "seed=%u stack=%d blinds=%d/%d\n",
	              profile_name(cli.profile), static_cast<unsigned>(plan.planned_bytes() / 1024u),
	              static_cast<unsigned>(cli.seats), static_cast<unsigned>(cli.hands),
	              static_cast<unsigned>(cli.sessions), static_cast<unsigned>(cli.seed),
	              static_cast<int>(cli.stack), static_cast<int>(cli.small_blind),
	              static_cast<int>(cli.big_blind));
	put(line);
	std::snprintf(line, sizeof(line), "tabla preflop=%s muestras=%u rango=%s clases=%u\n",
	              table.ready ? "si" : "no", static_cast<unsigned>(cli.table_samples),
	              cli.use_range ? (opponent_range.class_count() > 0u ? "si" : "vacio") : "no",
	              static_cast<unsigned>(opponent_range.class_count()));
	put(line);
	put("\nasiento  estilo             net     bb/100   manos\n");
	for (u8 i = 0u; i < cli.seats; ++i) {
		const s32 centi = total.bb_per_100_centi(i, cli.big_blind);
		const s32 abs_centi = centi < 0 ? -centi : centi;
		std::snprintf(line, sizeof(line), "  %u     %-16s %+6d  %+7d.%02d  %u\n",
		              static_cast<unsigned>(i), style_name(cfg.styles[i]), static_cast<int>(total.net[i]),
		              static_cast<int>(centi / 100), static_cast<int>(abs_centi % 100),
		              static_cast<unsigned>(total.hands_played));
		put(line);
	}
	std::snprintf(line, sizeof(line),
	              "\ntotales: manos=%u showdowns=%u retiradas=%u subidas=%u igualadas=%u plegadas=%u "
	              "max_acciones=%u\n",
	              static_cast<unsigned>(total.hands_played), static_cast<unsigned>(total.showdowns),
	              static_cast<unsigned>(total.fold_wins), static_cast<unsigned>(total.raises),
	              static_cast<unsigned>(total.calls), static_cast<unsigned>(total.folds),
	              static_cast<unsigned>(total.max_actions_in_hand));
	put(line);

	if (!cli.quiet) {
		std::fputs(report.c_str(), stdout);
	}
	if (!cli.out.empty()) {
		const std::filesystem::path path = cli.out;
		if (path.has_parent_path()) {
			std::filesystem::create_directories(path.parent_path());
		}
		if (std::FILE* file = std::fopen(cli.out.c_str(), "wb")) {
			std::fwrite(report.data(), 1u, report.size(), file);
			std::fclose(file);
			if (!cli.quiet) {
				std::printf("\ninforme: %s\n", cli.out.c_str());
			}
		} else {
			std::fprintf(stderr, "no se pudo escribir %s\n", cli.out.c_str());
			return 1;
		}
	}
	return 0;
}
