// ============================================================================
// Test HOST-195: comodines (jokers) en baraja, evaluador y showdown
// ============================================================================
//
// Valida `kMaxDeckSize`/`card_is_joker`, el mazo de 54 cartas, la sustitucion de
// comodines en `evaluate_hand` (mejor carta posible) y la heuristica preflop.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/cards/195_cards_wildcards

#include <cstdio>

#include <eng/core/math/random.hpp>

#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/equity.hpp>
#include <eng/cards/rules/hand_rank.hpp>
#include <eng/cards/rules/texas_holdem.hpp>

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
constexpr Card joker_a = kJoker;
constexpr Card joker_b = static_cast<Card>(kJoker + 1u);

void test_deck_jokers() {
	Deck normal;
	normal.reset(false);
	check(normal.remaining_count == kDeckSize, "mazo: 52 sin comodines");
	check(!normal.contains(joker_a), "mazo: sin comodin por defecto");

	Deck wild;
	wild.reset(true);
	check(wild.remaining_count == kMaxDeckSize, "mazo: 54 con comodines");
	check(wild.contains(joker_a) && wild.contains(joker_b), "mazo: contiene los dos comodines");
	check(card_is_joker(joker_a) && card_is_joker(joker_b), "mazo: card_is_joker");
	check(!card_is_joker(c(Rank::Ace, Suit::Spades)), "mazo: un as no es comodin");
	check(card_playable(joker_a) && card_playable(c(Rank::Two, Suit::Clubs)),
	      "mazo: card_playable normal y comodin");
}

void test_wild_evaluation() {
	// Un comodin completa la escalera de color real.
	const Card royal[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades),
	                     c(Rank::Queen, Suit::Spades), c(Rank::Jack, Suit::Spades), joker_a};
	check(hand_category(evaluate_hand(royal)) == HandCategory::StraightFlush,
	      "comodin: completa escalera de color");

	// Dos comodines tambien.
	const Card royal2[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades),
	                      c(Rank::Queen, Suit::Spades), joker_a, joker_b};
	check(hand_category(evaluate_hand(royal2)) == HandCategory::StraightFlush,
	      "comodin: dos comodines completan escalera de color");

	// Un comodin completa el poker de ases.
	const Card quads[5] {c(Rank::Ace, Suit::Hearts), c(Rank::Ace, Suit::Diamonds),
	                     c(Rank::Ace, Suit::Clubs), joker_a, c(Rank::Two, Suit::Clubs)};
	check(hand_category(evaluate_hand(quads)) == HandCategory::Quads,
	      "comodin: completa poker");

	// Sin comodines el resultado no cambia.
	const Card plain[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades),
	                     c(Rank::Queen, Suit::Spades), c(Rank::Jack, Suit::Spades),
	                     c(Rank::Ten, Suit::Spades)};
	check(hand_category(evaluate_hand(plain)) == HandCategory::StraightFlush,
	      "comodin: sin comodin no cambia");
	check(evaluate_hand(plain) == evaluate_plain(plain),
	      "comodin: evaluate_hand == evaluate_plain sin comodines");

	// Un comodin solo no llega a 5 cartas: sin valor.
	const Card two[2] {c(Rank::Ace, Suit::Spades), joker_a};
	check(evaluate_hand(two) == kHandValueNone, "comodin: menos de 5 sin valor");
}

void test_preflop_joker() {
	const u16 ace_king = preflop_strength_permille(joker_a, c(Rank::King, Suit::Spades));
	const u16 seven_two = preflop_strength_permille(c(Rank::Seven, Suit::Spades),
	                                                c(Rank::Two, Suit::Hearts));
	check(ace_king > 500u, "comodin: as+rey es fuerte");
	check(ace_king > seven_two, "comodin: as+rey > 72o");
}

void test_wild_showdown() {
	Table t {};
	t.seat_count = 2u;
	t.variant = PokerVariant::TexasHoldem;
	t.with_jokers = true;
	t.board_count = kBoardCards;
	t.board[0] = c(Rank::Two, Suit::Clubs);
	t.board[1] = c(Rank::Seven, Suit::Spades);
	t.board[2] = c(Rank::Nine, Suit::Diamonds);
	t.board[3] = c(Rank::Jack, Suit::Clubs);
	t.board[4] = c(Rank::Three, Suit::Hearts);
	for (u8 i = 0u; i < 2u; ++i) {
		t.seats[i].stack = 1000;
		t.seats[i].status = SeatStatus::Active;
		t.seats[i].committed = 100;
	}
	t.seats[0].hole[0] = c(Rank::Ace, Suit::Spades);
	t.seats[0].hole[1] = c(Rank::Ace, Suit::Diamonds);
	t.seats[1].hole[0] = joker_a;
	t.seats[1].hole[1] = c(Rank::King, Suit::Spades);

	settle_showdown(t);
	check(t.seats[0].stack == 1200 && t.seats[1].stack == 1000,
	      "comodin: la pareja de ases gana al comodin");
}

} // namespace

int main() {
	std::printf("eng::cards wildcards:\n");
	test_deck_jokers();
	test_wild_evaluation();
	test_preflop_joker();
	test_wild_showdown();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards wildcards (mazo 54, sustitucion y showdown)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
