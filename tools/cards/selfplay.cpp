// ============================================================================
// cards selfplay: partidas de poker completas CPU vs CPU en host para ajustar el
// nivel (estilos, perfiles de memoria, tabla preflop y rango de rival), sin UI ni
// emulador. Mide net por asiento, bb/100, showdowns y actividad, y permite barrer
// perfiles (`--sweep`) y comparar contra una referencia (`--compare`) con CSV.
// ============================================================================
//
// Reutiliza las mismas piezas que el juego consumira:
//   - `eng::cards::run_session` (sim/session.hpp) con boton rotando y recompra.
//   - estilos `BotStyle` (ai/bot.hpp) ortogonales al footprint.
//   - perfiles `CardPlan` N20..N512 (core/budget.hpp).
//   - tabla preflop de 169 clases y rango de rival (eval/range.hpp); el rango puede
//     ser fijo (`table`), dinamico desde el modelo de rival (`dynamic`) o desactivado.
//
// Uso:
//   selfplay.sh [hands] [--seats N] [--seed S] [--stack N] [--sb N] [--bb N]
//               [--profile N20|N64|N128|N256|N512] [--sessions N]
//               [--table-samples N] [--range-classes N] [--range-mode dynamic|table|none]
//               [--styles tp,ta,lp,la,eq] [--variant holdem|omaha] [--jokers]
//               [--compare] [--sweep] [--csv ruta.csv]
//               [--out ruta.txt] [--quiet]
//   por defecto: 200 --seats 6 --seed 1 --profile N512 --range-mode dynamic
//
// Salida: informe por asiento (estilo, net, bb/100) y totales. Con `--out` se escribe
// a `out/cards/selfplay/` (por defecto); con `--csv` se vuelca la tabla de comparacion.

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

enum class RangeMode : u8 { Dynamic = 0u, Table, None };

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
	RangeMode range_mode = RangeMode::Dynamic;
	bool compare = false;
	bool sweep = false;
	bool jokers = false;
	PokerVariant variant = PokerVariant::TexasHoldem;
	bool quiet = false;
	std::string out = "out/cards/selfplay/selfplay.txt";
	std::string csv;
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
	if (name == "tp" || name == "tight-passive") {
		return BotStyle::TightPassive;
	}
	if (name == "ta" || name == "tight-aggressive") {
		return BotStyle::TightAggressive;
	}
	if (name == "lp" || name == "loose-passive") {
		return BotStyle::LoosePassive;
	}
	if (name == "la" || name == "loose-aggressive") {
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

[[nodiscard]] const char* style_key(BotStyle style) {
	switch (style) {
	case BotStyle::TightPassive:
		return "tp";
	case BotStyle::TightAggressive:
		return "ta";
	case BotStyle::LoosePassive:
		return "lp";
	case BotStyle::LooseAggressive:
		return "la";
	case BotStyle::Balanced:
		return "eq";
	case BotStyle::Count:
		break;
	}
	return "?";
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

/// Estilos por defecto: se rotan para que la mesa enfrente estilos distintos.
BotStyle default_style(u8 seat) {
	constexpr BotStyle kOrder[5] = {BotStyle::TightAggressive, BotStyle::LoosePassive,
	                                BotStyle::Balanced, BotStyle::LooseAggressive,
	                                BotStyle::TightPassive};
	return kOrder[seat % 5u];
}

void apply_styles(const Cli& cli, SessionConfig& cfg) {
	cfg.variant = cli.variant;
	cfg.with_jokers = cli.jokers;
	for (u8 i = 0u; i < kSeats; ++i) {
		cfg.styles[i] = default_style(i);
	}
	if (cli.compare) {
		// Asiento 0 = referencia (tight-agresivo); el resto, campo variado.
		constexpr BotStyle kField[4] = {BotStyle::LoosePassive, BotStyle::Balanced,
		                                BotStyle::LooseAggressive, BotStyle::TightPassive};
		for (u8 i = 1u; i < kSeats; ++i) {
			cfg.styles[i] = kField[(i - 1u) % 4u];
		}
		cfg.styles[0] = BotStyle::TightAggressive;
	}
	for (u8 i = 0u; i < cli.style_count && i < cli.seats; ++i) {
		cfg.styles[i] = cli.styles[i];
	}
}

/// Juega `cli.sessions` sesiones con `plan` y acumula en `total`.
void run_profile(const Cli& cli, const CardPlan& plan, const PreflopTable& table,
                 const HandRange& table_range, SessionConfig cfg, SessionStats& total) {
	total = SessionStats {};
	total.starting_stack = cli.stack;

	const PreflopTable* use_table = nullptr;
	const HandRange* use_range = nullptr;
	if (cli.range_mode != RangeMode::None) {
		use_table = table.ready ? &table : nullptr;
		if (cli.range_mode == RangeMode::Table) {
			use_range = &table_range;
		}
	}

	for (u16 session = 0u; session < cli.sessions; ++session) {
		SessionConfig run_cfg = cfg;
		run_cfg.seed = cli.seed + static_cast<u32>(session);
		SessionStats stats {};
		run_session(run_cfg, plan, stats, nullptr, use_table, use_range);
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
}

void append_seat_row(std::string& csv, const Cli& cli, CardProfile profile, const SessionConfig& cfg,
                     const SessionStats& stats, u8 seat, const char* mode) {
	char line[160];
	std::snprintf(line, sizeof(line), "%s,%s,%u,%s,%d,%d,%u,%u,%u,%u,%u\n", mode,
	              profile_name(profile), static_cast<unsigned>(seat), style_key(cfg.styles[seat]),
	              static_cast<int>(stats.net[seat]),
	              static_cast<int>(stats.bb_per_100_centi(seat, cli.big_blind)),
	              static_cast<unsigned>(stats.hands_played), static_cast<unsigned>(stats.showdowns),
	              static_cast<unsigned>(stats.raises), static_cast<unsigned>(stats.calls),
	              static_cast<unsigned>(stats.folds));
	csv += line;
}

} // namespace

int main(int argc, char** argv) {
	Cli cli {};

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
		} else if (arg == "--range-mode") {
			const std::string mode = next("--range-mode");
			if (mode == "dynamic") {
				cli.range_mode = RangeMode::Dynamic;
			} else if (mode == "table") {
				cli.range_mode = RangeMode::Table;
			} else if (mode == "none") {
				cli.range_mode = RangeMode::None;
			} else {
				std::fprintf(stderr, "range-mode desconocido; usa dynamic|table|none\n");
				return 2;
			}
		} else if (arg == "--profile") {
			if (!parse_profile(next("--profile"), cli.profile)) {
				std::fprintf(stderr, "perfil desconocido; usa N20|N64|N128|N256|N512\n");
				return 2;
			}
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
		} else if (arg == "--compare") {
			cli.compare = true;
		} else if (arg == "--sweep") {
			cli.sweep = true;
		} else if (arg == "--variant") {
			const std::string variant = next("--variant");
			if (variant == "omaha") {
				cli.variant = PokerVariant::Omaha;
			} else if (variant == "holdem") {
				cli.variant = PokerVariant::TexasHoldem;
			} else {
				std::fprintf(stderr, "variant desconocida; usa holdem|omaha\n");
				return 2;
			}
		} else if (arg == "--jokers") {
			cli.jokers = true;
		} else if (arg == "--csv") {
			cli.csv = next("--csv");
		} else if (arg == "--out") {
			cli.out = next("--out");
		} else if (arg == "--quiet") {
			cli.quiet = true;
		}
	}

	// Tabla preflop (una vez) y rango fijo derivado de ella (para `--range-mode table`).
	eng::Xoroshiro64pp table_rng {cli.seed ^ 0x51ed2701u, cli.seed + 0x9e3779b9u};
	PreflopTable table;
	HandRange table_range;
	build_preflop_table(table, table_rng, cli.table_samples);
	make_range_by_equity(table, table_range, cli.range_classes);

	std::string report;
	report.reserve(4096);
	char line[256];
	std::snprintf(line, sizeof(line),
	              "== cards selfplay ==\nseats=%u hands=%u sessions=%u seed=%u stack=%d blinds=%d/%d "
	              "range=%s tabla=%s muestras=%u\n",
	              static_cast<unsigned>(cli.seats), static_cast<unsigned>(cli.hands),
	              static_cast<unsigned>(cli.sessions), static_cast<unsigned>(cli.seed),
	              static_cast<int>(cli.stack), static_cast<int>(cli.small_blind),
	              static_cast<int>(cli.big_blind),
	              cli.range_mode == RangeMode::Dynamic   ? "dynamic"
	              : cli.range_mode == RangeMode::Table ? "table"
	                                                   : "none",
	              table.ready ? "si" : "no", static_cast<unsigned>(cli.table_samples));
	report += line;

	std::string csv;
	if (!cli.csv.empty()) {
		csv += "mode,profile,seat,style,net,bb100_centi,hands,showdowns,raises,calls,folds\n";
	}

	if (cli.sweep) {
		constexpr CardProfile kProfiles[5] = {CardProfile::N20, CardProfile::N64, CardProfile::N128,
		                                      CardProfile::N256, CardProfile::N512};
		std::snprintf(line, sizeof(line), "\nbarrido de perfiles (asiento 0 = referencia):\n");
		report += line;
		std::snprintf(line, sizeof(line), "perfil   plan(kB)  ref_bb/100   showdowns  subidas\n");
		report += line;
		for (u8 p = 0u; p < 5u; ++p) {
			const CardPlan plan = card_profile_plan(kProfiles[p]);
			SessionConfig cfg {};
			cfg.seats = cli.seats;
			cfg.starting_stack = cli.stack;
			cfg.small_blind = cli.small_blind;
			cfg.big_blind = cli.big_blind;
			cfg.hands = cli.hands;
			cfg.seed = cli.seed;
			apply_styles(cli, cfg);
			SessionStats stats {};
			run_profile(cli, plan, table, table_range, cfg, stats);
			const s32 centi = stats.bb_per_100_centi(0u, cli.big_blind);
			const s32 abs_centi = centi < 0 ? -centi : centi;
			std::snprintf(line, sizeof(line), "%-7s  %6u   %+8d.%02d   %8u  %7u\n",
			              profile_name(kProfiles[p]),
			              static_cast<unsigned>(plan.planned_bytes() / 1024u),
			              static_cast<int>(centi / 100), static_cast<int>(abs_centi % 100),
			              static_cast<unsigned>(stats.showdowns),
			              static_cast<unsigned>(stats.raises));
			report += line;
			if (!cli.csv.empty()) {
				append_seat_row(csv, cli, kProfiles[p], cfg, stats, 0u, "sweep");
			}
		}
	} else {
		const CardPlan plan = card_profile_plan(cli.profile);
		SessionConfig cfg {};
		cfg.seats = cli.seats;
		cfg.starting_stack = cli.stack;
		cfg.small_blind = cli.small_blind;
		cfg.big_blind = cli.big_blind;
		cfg.hands = cli.hands;
		cfg.seed = cli.seed;
		apply_styles(cli, cfg);

		SessionStats total {};
		run_profile(cli, plan, table, table_range, cfg, total);

		std::snprintf(line, sizeof(line), "perfil=%s plan=%u kB compare=%s\n",
		              profile_name(cli.profile), static_cast<unsigned>(plan.planned_bytes() / 1024u),
		              cli.compare ? "si" : "no");
		report += line;
		report += "\nasiento  estilo             net     bb/100   manos\n";
		for (u8 i = 0u; i < cli.seats; ++i) {
			const s32 centi = total.bb_per_100_centi(i, cli.big_blind);
			const s32 abs_centi = centi < 0 ? -centi : centi;
			std::snprintf(line, sizeof(line), "  %u     %-16s %+6d  %+7d.%02d  %u\n",
			              static_cast<unsigned>(i), style_name(cfg.styles[i]),
			              static_cast<int>(total.net[i]), static_cast<int>(centi / 100),
			              static_cast<int>(abs_centi % 100),
			              static_cast<unsigned>(total.hands_played));
			report += line;
			if (!cli.csv.empty()) {
				append_seat_row(csv, cli, cli.profile, cfg, total, i, "single");
			}
		}
		std::snprintf(line, sizeof(line),
		              "\ntotales: manos=%u showdowns=%u retiradas=%u subidas=%u igualadas=%u "
		              "plegadas=%u max_acciones=%u\n",
		              static_cast<unsigned>(total.hands_played),
		              static_cast<unsigned>(total.showdowns),
		              static_cast<unsigned>(total.fold_wins), static_cast<unsigned>(total.raises),
		              static_cast<unsigned>(total.calls), static_cast<unsigned>(total.folds),
		              static_cast<unsigned>(total.max_actions_in_hand));
		report += line;
	}

	if (!cli.quiet) {
		std::fputs(report.c_str(), stdout);
	}
	static const auto write_file = [](const std::string& path, const std::string& data) -> bool {
		if (path.empty()) {
			return true;
		}
		const std::filesystem::path fs_path = path;
		if (fs_path.has_parent_path()) {
			std::filesystem::create_directories(fs_path.parent_path());
		}
		std::FILE* file = std::fopen(path.c_str(), "wb");
		if (file == nullptr) {
			std::fprintf(stderr, "no se pudo escribir %s\n", path.c_str());
			return false;
		}
		std::fwrite(data.data(), 1u, data.size(), file);
		std::fclose(file);
		return true;
	};
	if (!write_file(cli.out, report)) {
		return 1;
	}
	if (!write_file(cli.csv, csv)) {
		return 1;
	}
	if (!cli.quiet) {
		std::printf("\ninforme: %s\n", cli.out.c_str());
		if (!cli.csv.empty()) {
			std::printf("csv: %s\n", cli.csv.c_str());
		}
	}
	return 0;
}
