// ============================================================================
// Test HOST-192: simulacion de partidas entre bots (eng::cards sim + ai)
// ============================================================================
//
// Juega sesiones completas con semilla fija y valida conservacion de fichas,
// determinismo, actividad (subidas y showdowns) y que los perfiles N20 y N512
// juegan con el mismo flujo. Es la herramienta de ajuste de nivel desde host.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/cards/192_cards_selfplay

#include <cstdio>

#include <eng/cards/core/budget.hpp>
#include <eng/cards/sim/session.hpp>

namespace {

using namespace eng::cards;
using namespace eng;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void configure(SessionConfig& cfg) {
	cfg.seats = 6u;
	cfg.starting_stack = 1000;
	cfg.small_blind = 5;
	cfg.big_blind = 10;
	cfg.hands = 120u;
	cfg.seed = 20260918u;
	cfg.styles[0] = BotStyle::TightAggressive;
	cfg.styles[1] = BotStyle::LoosePassive;
	cfg.styles[2] = BotStyle::Balanced;
	cfg.styles[3] = BotStyle::LooseAggressive;
	cfg.styles[4] = BotStyle::TightPassive;
	cfg.styles[5] = BotStyle::Balanced;
}

s32 net_sum(const SessionStats& stats, u8 seats) {
	s32 sum = 0;
	for (u8 i = 0u; i < seats; ++i) {
		sum += stats.net[i];
	}
	return sum;
}

void test_session() {
	SessionConfig cfg {};
	configure(cfg);
	const CardPlan plan = plan_cards_memory(524288u);

	SessionStats a {};
	run_session(cfg, plan, a);
	check(a.hands_played == cfg.hands, "sesion: numero de manos");
	check(net_sum(a, cfg.seats) == 0, "sesion: conservacion de fichas (suma net = 0)");
	check(a.raises > 0u, "sesion: hay subidas");
	check(a.calls > 0u, "sesion: hay igualadas");
	check(a.showdowns > 0u, "sesion: hay showdowns");
	check(a.showdowns + a.fold_wins == cfg.hands, "sesion: cada mano termina de una forma");

	// Determinismo: misma semilla => mismos resultados.
	SessionStats b {};
	run_session(cfg, plan, b);
	bool same = a.net[0] == b.net[0] && a.net[1] == b.net[1] && a.net[2] == b.net[2] &&
	            a.raises == b.raises && a.showdowns == b.showdowns;
	check(same, "sesion: determinista por semilla");

	// Otra semilla => resultados distintos (probable).
	SessionConfig cfg2 = cfg;
	cfg2.seed = cfg.seed + 1u;
	SessionStats c {};
	run_session(cfg2, plan, c);
	check(!(a.net[0] == c.net[0] && a.net[1] == c.net[1] && a.net[2] == c.net[2]),
	      "sesion: semilla distinta => reparto distinto");
}

void test_low_profile() {
	SessionConfig cfg {};
	configure(cfg);
	cfg.hands = 60u;
	const CardPlan plan = plan_cards_memory(20000u);
	check(plan.mc_samples == 0u, "N20: sin Monte Carlo");

	SessionStats stats {};
	run_session(cfg, plan, stats);
	check(stats.hands_played == cfg.hands, "N20: manos jugadas");
	check(net_sum(stats, cfg.seats) == 0, "N20: conservacion de fichas");
	check(stats.raises + stats.calls + stats.folds > 0u, "N20: actividad");
}

void test_bb_per_100() {
	SessionStats stats {};
	stats.net[0] = 250;
	stats.hands_played = 100u;
	check(stats.bb_per_100_centi(0, 10) == 2500, "bb/100: +25 bb => 2500 centi-bb");
}

} // namespace

int main() {
	std::printf("eng::cards selfplay:\n");
	test_session();
	test_low_profile();
	test_bb_per_100();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards selfplay (conservacion, determinismo, N20/N512)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
