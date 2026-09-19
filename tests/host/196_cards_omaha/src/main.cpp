// ============================================================================
// Test HOST-196: Omaha de extremo a extremo (equity y sesion CPU vs CPU)
// ============================================================================
//
// Valida el nucleo de equity agnostico de variante (`OmahaRanker`) y que
// `run_session` juega Omaha con conservacion de fichas y determinismo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/169_cards_omaha

#include <cstdio>

#include <eng/cards/core/budget.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/equity.hpp>
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

Card c(Rank r, Suit s) { return make_card(r, s); }

void test_omaha_equity() {
	const Card aces[4] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                    c(Rank::King, Suit::Spades), c(Rank::King, Suit::Hearts)};
	const Card trash[4] {c(Rank::Two, Suit::Clubs), c(Rank::Three, Suit::Diamonds),
	                     c(Rank::Seven, Suit::Hearts), c(Rank::Nine, Suit::Spades)};

	eng::Xoroshiro64pp rng_a {11u, 22u};
	const EquityResult eq_aces = equity_vs_random_omaha(
	    eng::Span<const Card> {aces, 4u}, eng::Span<const Card> {}, 1u, 200u, rng_a);
	eng::Xoroshiro64pp rng_b {11u, 22u};
	const EquityResult eq_trash = equity_vs_random_omaha(
	    eng::Span<const Card> {trash, 4u}, eng::Span<const Card> {}, 1u, 200u, rng_b);

	check(eq_aces.equity_permille > eq_trash.equity_permille, "omaha: AAxx > basura");
	check(eq_aces.equity_permille > 600u, "omaha: AAxx domina");
	check(eq_trash.equity_permille < 450u, "omaha: basura por debajo de 50%");

	// Determinismo por semilla.
	eng::Xoroshiro64pp rng_c {11u, 22u};
	const EquityResult again = equity_vs_random_omaha(
	    eng::Span<const Card> {aces, 4u}, eng::Span<const Card> {}, 1u, 200u, rng_c);
	check(eq_aces.equity_permille == again.equity_permille, "omaha: determinista");
}

void test_omaha_session() {
	SessionConfig cfg {};
	cfg.seats = 3u;
	cfg.starting_stack = 1000;
	cfg.small_blind = 5;
	cfg.big_blind = 10;
	cfg.hands = 40u;
	cfg.seed = 101u;
	cfg.variant = PokerVariant::Omaha;
	cfg.styles[0] = BotStyle::TightAggressive;
	cfg.styles[1] = BotStyle::Balanced;
	cfg.styles[2] = BotStyle::LoosePassive;

	const CardPlan plan = plan_cards_memory(65536u);
	SessionStats stats {};
	run_session(cfg, plan, stats);
	check(stats.hands_played == cfg.hands, "omaha: manos jugadas");
	s32 sum = 0;
	for (u8 i = 0u; i < cfg.seats; ++i) {
		sum += stats.net[i];
	}
	check(sum == 0, "omaha: conservacion de fichas");
	check(stats.raises > 0u && stats.calls > 0u, "omaha: actividad");

	SessionStats again {};
	run_session(cfg, plan, again);
	bool same = true;
	for (u8 i = 0u; i < cfg.seats; ++i) {
		if (stats.net[i] != again.net[i]) {
			same = false;
		}
	}
	check(same, "omaha: determinista por semilla");
}

void test_omaha_with_jokers() {
	// Una sesion Omaha con comodines no debe corromper las fichas.
	SessionConfig cfg {};
	cfg.seats = 3u;
	cfg.starting_stack = 1000;
	cfg.small_blind = 5;
	cfg.big_blind = 10;
	cfg.hands = 20u;
	cfg.seed = 55u;
	cfg.variant = PokerVariant::Omaha;
	cfg.with_jokers = true;
	const CardPlan plan = plan_cards_memory(65536u);
	SessionStats stats {};
	run_session(cfg, plan, stats);
	s32 sum = 0;
	for (u8 i = 0u; i < cfg.seats; ++i) {
		sum += stats.net[i];
	}
	check(stats.hands_played == cfg.hands && sum == 0, "omaha+comodines: conservacion");
}

} // namespace

int main() {
	std::printf("eng::cards omaha:\n");
	test_omaha_equity();
	test_omaha_session();
	test_omaha_with_jokers();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards omaha (equity 4 cartas, sesion y comodines)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
