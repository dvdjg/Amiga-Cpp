// ============================================================================
// Test HOST-191: equity Monte Carlo (eng::cards/eval/equity.hpp)
// ============================================================================
//
// Valida el equity contra rivales aleatorios (determinista por semilla), el
// fallback heuristico preflop y las pot odds. Comprueba relaciones robustas
// (AA > 72o, color hecho gana siempre) que no dependen del muestreo exacto.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/164_cards_equity

#include <cstdio>

#include <eng/core/random.hpp>

#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/equity.hpp>

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

void test_preflop_heuristic() {
	const u16 aa = preflop_strength_permille(c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts));
	const u16 kk = preflop_strength_permille(c(Rank::King, Suit::Spades), c(Rank::King, Suit::Hearts));
	const u16 seven_two = preflop_strength_permille(c(Rank::Seven, Suit::Spades),
	                                                c(Rank::Two, Suit::Hearts));
	check(aa > kk, "preflop: AA > KK");
	check(kk > seven_two, "preflop: KK > 72o");
	check(aa > 900u, "preflop: AA muy fuerte");
	check(seven_two < 250u, "preflop: 72o debil");
}

void test_equity_ordering() {
	const Card aa[2] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts)};
	const Card seven_two[2] {c(Rank::Seven, Suit::Spades), c(Rank::Two, Suit::Hearts)};

	eng::Xoroshiro64pp rng_a {42u, 99u};
	const EquityResult eq_aa = equity_vs_random(eng::Span<const Card> {aa, 2u}, eng::Span<const Card> {}, 1u, 400u, rng_a);

	eng::Xoroshiro64pp rng_b {42u, 99u};
	const EquityResult eq_72 = equity_vs_random(eng::Span<const Card> {seven_two, 2u}, eng::Span<const Card> {}, 1u, 400u, rng_b);

	check(eq_aa.equity_permille > eq_72.equity_permille, "equity: AA > 72o");
	check(eq_aa.equity_permille > 750u, "equity: AA domina");
	check(eq_72.equity_permille < 500u, "equity: 72o esta por debajo del 50%");

	// Determinismo.
	eng::Xoroshiro64pp rng_c {42u, 99u};
	const EquityResult eq_aa2 = equity_vs_random(eng::Span<const Card> {aa, 2u}, eng::Span<const Card> {}, 1u, 400u, rng_c);
	check(eq_aa.equity_permille == eq_aa2.equity_permille, "equity: determinista por semilla");
}

void test_made_hand() {
	// Hero con escalera de color real en el tablero: no puede perder.
	const Card hole[2] {c(Rank::Ace, Suit::Hearts), c(Rank::King, Suit::Hearts)};
	const Card board[5] {c(Rank::Queen, Suit::Hearts), c(Rank::Jack, Suit::Hearts),
	                     c(Rank::Ten, Suit::Hearts), c(Rank::Two, Suit::Clubs),
	                     c(Rank::Three, Suit::Diamonds)};
	eng::Xoroshiro64pp rng {5u, 6u};
	const EquityResult eq = equity_vs_random(eng::Span<const Card> {hole, 2u},
	                                         eng::Span<const Card> {board, 5u}, 2u, 64u, rng);
	check(eq.equity_permille == 1000u, "equity: escalera de color gana siempre");
}

void test_pot_odds() {
	check(pot_odds_permille(100, 300) == 250u, "pot odds: 100 en 300 => 250");
	check(pot_odds_permille(0, 300) == 0u, "pot odds: sin llamada => 0");
	check(pot_odds_permille(50, 50) == 500u, "pot odds: 50 en 50 => 500");
}

} // namespace

int main() {
	std::printf("eng::cards equity:\n");
	test_preflop_heuristic();
	test_equity_ordering();
	test_made_hand();
	test_pot_odds();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards equity (Monte Carlo determinista, heuristica y pot odds)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
