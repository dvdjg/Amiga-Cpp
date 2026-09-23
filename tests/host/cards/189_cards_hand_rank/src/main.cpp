// ============================================================================
// Test HOST-189: evaluador de manos de poker (eng::cards/rules/hand_rank.hpp)
// ============================================================================
//
// Comprueba categorias conocidas, orden total entre ellas, casos limite
// (escalera de as bajo, color + escalera, mejor de 7) y empates por kicker.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/162_cards_hand_rank

#include <cstdio>

#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/hand_rank.hpp>

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

void test_categories() {
	const Card royal[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades),
	                     c(Rank::Queen, Suit::Spades), c(Rank::Jack, Suit::Spades),
	                     c(Rank::Ten, Suit::Spades)};
	check(hand_category(evaluate5(royal)) == HandCategory::StraightFlush, "royal flush");

	const Card quads[5] {c(Rank::Nine, Suit::Spades), c(Rank::Nine, Suit::Hearts),
	                     c(Rank::Nine, Suit::Diamonds), c(Rank::Nine, Suit::Clubs),
	                     c(Rank::Two, Suit::Spades)};
	check(hand_category(evaluate5(quads)) == HandCategory::Quads, "poker");

	const Card full[5] {c(Rank::King, Suit::Spades), c(Rank::King, Suit::Hearts),
	                    c(Rank::King, Suit::Diamonds), c(Rank::Two, Suit::Spades),
	                    c(Rank::Two, Suit::Hearts)};
	check(hand_category(evaluate5(full)) == HandCategory::FullHouse, "full");

	const Card flush[5] {c(Rank::Ace, Suit::Hearts), c(Rank::Jack, Suit::Hearts),
	                     c(Rank::Eight, Suit::Hearts), c(Rank::Five, Suit::Hearts),
	                     c(Rank::Three, Suit::Hearts)};
	check(hand_category(evaluate5(flush)) == HandCategory::Flush, "color");

	const Card straight[5] {c(Rank::Nine, Suit::Spades), c(Rank::Eight, Suit::Hearts),
	                        c(Rank::Seven, Suit::Diamonds), c(Rank::Six, Suit::Clubs),
	                        c(Rank::Five, Suit::Spades)};
	check(hand_category(evaluate5(straight)) == HandCategory::Straight, "escalera");

	const Card wheel[5] {c(Rank::Ace, Suit::Spades), c(Rank::Two, Suit::Hearts),
	                     c(Rank::Three, Suit::Diamonds), c(Rank::Four, Suit::Clubs),
	                     c(Rank::Five, Suit::Spades)};
	check(hand_category(evaluate5(wheel)) == HandCategory::Straight, "escalera de as bajo");

	const Card trips[5] {c(Rank::Seven, Suit::Spades), c(Rank::Seven, Suit::Hearts),
	                     c(Rank::Seven, Suit::Diamonds), c(Rank::Ace, Suit::Clubs),
	                     c(Rank::King, Suit::Spades)};
	check(hand_category(evaluate5(trips)) == HandCategory::Trips, "trio");

	const Card two_pair[5] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                        c(Rank::King, Suit::Diamonds), c(Rank::King, Suit::Clubs),
	                        c(Rank::Queen, Suit::Spades)};
	check(hand_category(evaluate5(two_pair)) == HandCategory::TwoPair, "doble pareja");

	const Card pair[5] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                    c(Rank::King, Suit::Diamonds), c(Rank::Queen, Suit::Clubs),
	                    c(Rank::Jack, Suit::Spades)};
	check(hand_category(evaluate5(pair)) == HandCategory::Pair, "pareja");

	const Card high[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Hearts),
	                    c(Rank::Queen, Suit::Diamonds), c(Rank::Jack, Suit::Clubs),
	                    c(Rank::Nine, Suit::Spades)};
	check(hand_category(evaluate5(high)) == HandCategory::HighCard, "carta alta");
}

void test_ordering() {
	const Card royal[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades),
	                     c(Rank::Queen, Suit::Spades), c(Rank::Jack, Suit::Spades),
	                     c(Rank::Ten, Suit::Spades)};
	const Card quads[5] {c(Rank::Nine, Suit::Spades), c(Rank::Nine, Suit::Hearts),
	                     c(Rank::Nine, Suit::Diamonds), c(Rank::Nine, Suit::Clubs),
	                     c(Rank::Two, Suit::Spades)};
	const Card full[5] {c(Rank::King, Suit::Spades), c(Rank::King, Suit::Hearts),
	                    c(Rank::King, Suit::Diamonds), c(Rank::Two, Suit::Spades),
	                    c(Rank::Two, Suit::Hearts)};
	const Card flush[5] {c(Rank::Ace, Suit::Hearts), c(Rank::Jack, Suit::Hearts),
	                     c(Rank::Eight, Suit::Hearts), c(Rank::Five, Suit::Hearts),
	                     c(Rank::Three, Suit::Hearts)};
	const Card straight[5] {c(Rank::Nine, Suit::Spades), c(Rank::Eight, Suit::Hearts),
	                        c(Rank::Seven, Suit::Diamonds), c(Rank::Six, Suit::Clubs),
	                        c(Rank::Five, Suit::Spades)};
	const Card trips[5] {c(Rank::Seven, Suit::Spades), c(Rank::Seven, Suit::Hearts),
	                     c(Rank::Seven, Suit::Diamonds), c(Rank::Ace, Suit::Clubs),
	                     c(Rank::King, Suit::Spades)};
	const Card two_pair[5] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                        c(Rank::King, Suit::Diamonds), c(Rank::King, Suit::Clubs),
	                        c(Rank::Queen, Suit::Spades)};
	const Card pair[5] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                    c(Rank::King, Suit::Diamonds), c(Rank::Queen, Suit::Clubs),
	                    c(Rank::Jack, Suit::Spades)};
	const Card high[5] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Hearts),
	                    c(Rank::Queen, Suit::Diamonds), c(Rank::Jack, Suit::Clubs),
	                    c(Rank::Nine, Suit::Spades)};

	const HandValue v_royal = evaluate5(royal);
	const HandValue v_quads = evaluate5(quads);
	const HandValue v_full = evaluate5(full);
	const HandValue v_flush = evaluate5(flush);
	const HandValue v_straight = evaluate5(straight);
	const HandValue v_trips = evaluate5(trips);
	const HandValue v_two = evaluate5(two_pair);
	const HandValue v_pair = evaluate5(pair);
	const HandValue v_high = evaluate5(high);

	check(v_royal > v_quads, "orden: escalera de color > poker");
	check(v_quads > v_full, "orden: poker > full");
	check(v_full > v_flush, "orden: full > color");
	check(v_flush > v_straight, "orden: color > escalera");
	check(v_straight > v_trips, "orden: escalera > trio");
	check(v_trips > v_two, "orden: trio > doble pareja");
	check(v_two > v_pair, "orden: doble pareja > pareja");
	check(v_pair > v_high, "orden: pareja > carta alta");
}

void test_best_of_seven() {
	// Color (5 corazones) + escalera potencial A-K-Q-J-T en otros palos: gana color.
	const Card seven[7] {c(Rank::Ace, Suit::Hearts), c(Rank::King, Suit::Hearts),
	                     c(Rank::Queen, Suit::Hearts), c(Rank::Nine, Suit::Hearts),
	                     c(Rank::Two, Suit::Hearts), c(Rank::Jack, Suit::Spades),
	                     c(Rank::Ten, Suit::Clubs)};
	check(hand_category(evaluate_hand(seven)) == HandCategory::Flush,
	      "7 cartas: color gana a escalera");

	// Mejor de 7 con full house: K K K 2 2 + cartas irrelevantes.
	const Card full7[7] {c(Rank::King, Suit::Spades), c(Rank::King, Suit::Hearts),
	                     c(Rank::King, Suit::Diamonds), c(Rank::Two, Suit::Spades),
	                     c(Rank::Two, Suit::Hearts), c(Rank::Seven, Suit::Diamonds),
	                     c(Rank::Nine, Suit::Clubs)};
	check(hand_category(evaluate_hand(full7)) == HandCategory::FullHouse,
	      "7 cartas: full house");

	// Menos de 5 cartas: sin valor.
	const Card two[2] {c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Hearts)};
	check(evaluate_hand(two) == kHandValueNone, "menos de 5 cartas: sin valor");
}

void test_kickers() {
	const Card pair_ace_king_kicker[5] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                                    c(Rank::King, Suit::Diamonds), c(Rank::Queen, Suit::Clubs),
	                                    c(Rank::Jack, Suit::Spades)};
	const Card pair_ace_king_kicker2[5] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                                     c(Rank::King, Suit::Diamonds), c(Rank::Queen, Suit::Clubs),
	                                     c(Rank::Ten, Suit::Spades)};
	check(evaluate5(pair_ace_king_kicker) > evaluate5(pair_ace_king_kicker2),
	      "kicker: J supera a T con la misma pareja");

	// Misma mano => empate exacto.
	check(evaluate5(pair_ace_king_kicker) == evaluate5(pair_ace_king_kicker), "empate exacto");
}

} // namespace

int main() {
	std::printf("eng::cards hand_rank:\n");
	test_categories();
	test_ordering();
	test_best_of_seven();
	test_kickers();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards hand_rank (categorias, orden, mejor de 7, kickers)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
