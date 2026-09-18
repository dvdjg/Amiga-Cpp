// ============================================================================
// Test HOST-161: nucleo de eng::cards (tipos, baraja y presupuesto de memoria)
// ============================================================================
//
// Valida `engine/include/eng/cards/core/{types,deck,budget}.hpp`: empaquetado de
// carta, baraja determinista (mismo PRNG + misma semilla => misma permutacion),
// reparto/retirada de cartas y seleccion de perfil `N20`..`N512`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/161_cards_core

#include <cstdio>

#include <eng/core/random.hpp>

#include <eng/cards/core/budget.hpp>
#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/types.hpp>

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

void test_cards() {
	const Card ace_spades = make_card(Rank::Ace, Suit::Spades);
	check(card_rank(ace_spades) == Rank::Ace, "carta: rango As");
	check(card_suit(ace_spades) == Suit::Spades, "carta: palo picas");
	check(ace_spades == 51u, "carta: As de picas es la 51");

	const Card two_clubs = make_card(Rank::Two, Suit::Clubs);
	check(two_clubs == 0u, "carta: 2 de treboles es la 0");
	check(card_valid(two_clubs) && card_valid(ace_spades), "carta: validas");
	check(!card_valid(kNoCard), "carta: centinela invalida");

	check(hand_category_name(HandCategory::FullHouse).size() == 4u, "nombre: full");
	check(rank_name(Rank::Ten).size() == 1u, "nombre: T");
}

void test_deck() {
	Deck deck;
	deck.reset();
	check(deck.remaining_count == kDeckSize, "baraja: 52 cartas");
	check(deck.cards[0] == 0u && deck.cards[51] == 51u, "baraja: orden identidad");

	// Reparto desde el final.
	const Card first = deck.deal();
	check(first == 51u, "baraja: reparte desde el final");
	check(deck.remaining_count == 51u, "baraja: queda 51");

	// Retirada de carta conocida.
	deck.reset();
	check(deck.remove(make_card(Rank::Ace, Suit::Spades)), "baraja: retira As de picas");
	check(!deck.remove(make_card(Rank::Ace, Suit::Spades)), "baraja: no retira dos veces");
	check(deck.remaining_count == 51u, "baraja: 51 tras retirar");

	// Determinismo del barajado.
	Deck a;
	a.reset();
	eng::Xoroshiro64pp rng_a {123u, 456u};
	a.shuffle(rng_a);
	Deck b;
	b.reset();
	eng::Xoroshiro64pp rng_b {123u, 456u};
	b.shuffle(rng_b);
	bool same = true;
	for (u8 i = 0u; i < kDeckSize; ++i) {
		if (a.cards[i] != b.cards[i]) {
			same = false;
		}
	}
	check(same, "baraja: mismo PRNG+semilla => misma permutacion");

	// 52 cartas distintas tras barajar.
	bool seen[kDeckSize] {};
	bool unique = true;
	for (u8 i = 0u; i < kDeckSize; ++i) {
		if (seen[a.cards[i]]) {
			unique = false;
		}
		seen[a.cards[i]] = true;
	}
	check(unique, "baraja: las 52 cartas tras barajar son unicas");
}

void test_budget() {
	const CardPlan tiny = plan_cards_memory(20000u);
	check(tiny.profile == CardProfile::N20, "budget: 20 kB => N20");
	check(tiny.planned_bytes() <= 20000u, "budget: N20 cabe en 20 kB");
	check(tiny.mc_samples == 0u, "budget: N20 sin Monte Carlo");

	const CardPlan big = plan_cards_memory(524288u);
	check(big.profile == CardProfile::N512, "budget: 512 kB => N512");
	check(big.planned_bytes() <= 524288u, "budget: N512 cabe en 512 kB");
	check(big.mc_samples > 0u, "budget: N512 con Monte Carlo");

	const CardPlan mid = plan_cards_memory(70000u);
	check(mid.planned_bytes() <= 70000u, "budget: perfil intermedio cabe");

	// El plan crece con la RAM.
	check(card_profile_plan(CardProfile::N512).planned_bytes() >
	          card_profile_plan(CardProfile::N64).planned_bytes(),
	      "budget: N512 > N64");
}

} // namespace

int main() {
	std::printf("eng::cards core:\n");
	test_cards();
	test_deck();
	test_budget();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards core (tipos, baraja determinista, presupuesto N20-N512)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
