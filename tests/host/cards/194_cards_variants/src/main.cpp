// ============================================================================
// Test HOST-194: variantes de poker (Omaha) y estructura Limit
// ============================================================================
//
// Valida `eng/cards/rules/variants.hpp` (variante y estructura), el evaluador
// `evaluate_omaha` (exactamente 2 hole + 3 board) y la mesa en modo Limit (apuesta
// fija y tope de subidas por calle).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/cards/194_cards_variants

#include <cstdio>

#include <eng/core/math/random.hpp>

#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/hand_rank.hpp>
#include <eng/cards/rules/texas_holdem.hpp>
#include <eng/cards/rules/variants.hpp>

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

bool has(const Action* legal, u8 n, ActionType type) {
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == type) {
			return true;
		}
	}
	return false;
}

[[nodiscard]] s32 find_amount(const Action* legal, u8 n, ActionType type) {
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == type) {
			return legal[i].amount;
		}
	}
	return -1;
}

void test_omaha_evaluator() {
	// Tablero con escalera de color real: en Hold'em todos la tienen; en Omaha solo
	// quien tenga DOS corazones en la mano (necesita 2 hole + 3 board del mismo palo).
	const Card board[5] {c(Rank::Ace, Suit::Hearts), c(Rank::King, Suit::Hearts),
	                     c(Rank::Queen, Suit::Hearts), c(Rank::Jack, Suit::Hearts),
	                     c(Rank::Ten, Suit::Hearts)};

	const Card no_hearts[4] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Diamonds),
	                         c(Rank::King, Suit::Spades), c(Rank::King, Suit::Diamonds)};
	const HandValue v_no_hearts = evaluate_omaha(no_hearts, board);
	check(hand_category(v_no_hearts) != HandCategory::Flush &&
	          hand_category(v_no_hearts) != HandCategory::StraightFlush,
	      "omaha: sin dos corazones no hay color con el tablero");

	const Card two_hearts[4] {c(Rank::Two, Suit::Hearts), c(Rank::Three, Suit::Hearts),
	                          c(Rank::Two, Suit::Spades), c(Rank::Three, Suit::Spades)};
	check(hand_category(evaluate_omaha(two_hearts, board)) == HandCategory::Flush,
	      "omaha: dos corazones + tres del tablero = color");

	const Card straight_hearts[4] {c(Rank::Nine, Suit::Hearts), c(Rank::Ten, Suit::Hearts),
	                               c(Rank::Two, Suit::Spades), c(Rank::Three, Suit::Spades)};
	check(hand_category(evaluate_omaha(straight_hearts, board)) == HandCategory::StraightFlush,
	      "omaha: 9-10 de corazones + J-Q-K = escalera de color");

	// La misma mano de 7 en Hold'em (evaluador normal) sí da escalera de color.
	Card seven[7] {no_hearts[0], no_hearts[1], board[0], board[1], board[2], board[3], board[4]};
	check(hand_category(evaluate_hand(seven)) == HandCategory::StraightFlush,
	      "holdem: 2 hole + 5 board da escalera de color del tablero");
}

void test_variant_helpers() {
	check(hole_cards_for(PokerVariant::TexasHoldem) == 2u, "variante: Holdem reparte 2");
	check(hole_cards_for(PokerVariant::Omaha) == 4u, "variante: Omaha reparte 4");
	check(limit_bet_size(Street::Preflop, 5, 10) == 5, "limit: preflop = ciega pequena");
	check(limit_bet_size(Street::Flop, 5, 10) == 5, "limit: flop = ciega pequena");
	check(limit_bet_size(Street::Turn, 5, 10) == 10, "limit: turn = ciega grande");
	check(limit_bet_size(Street::River, 5, 10) == 10, "limit: river = ciega grande");
}

void test_variant_dealing() {
	eng::Xoroshiro64pp rng_holdem {21u, 22u};
	Table holdem;
	start_hand(holdem, rng_holdem, 3u, 1000, 5, 10, 0u, PokerVariant::TexasHoldem);
	check(holdem.seats[0].hole[0] != kNoCard && holdem.seats[0].hole[1] != kNoCard,
	      "reparto: Holdem 2 cartas");
	check(holdem.seats[0].hole[2] == kNoCard, "reparto: Holdem sin tercera carta");

	eng::Xoroshiro64pp rng_omaha {21u, 22u};
	Table omaha;
	start_hand(omaha, rng_omaha, 3u, 1000, 5, 10, 0u, PokerVariant::Omaha);
	bool ok = true;
	for (u8 s = 0u; s < 3u; ++s) {
		for (u8 i = 0u; i < 4u; ++i) {
			if (!card_valid(omaha.seats[s].hole[i])) {
				ok = false;
			}
			for (u8 j = static_cast<u8>(i + 1u); j < 4u; ++j) {
				if (omaha.seats[s].hole[i] == omaha.seats[s].hole[j]) {
					ok = false;
				}
			}
		}
	}
	check(ok, "reparto: Omaha 4 cartas validas y distintas");
}

void test_limit_betting() {
	eng::Xoroshiro64pp rng {31u, 32u};
	Table t {};
	start_hand(t, rng, 3u, 1000, 5, 10, 0u, PokerVariant::TexasHoldem, BettingStructure::Limit);
	check(t.structure == BettingStructure::Limit, "limit: estructura fija");

	Action legal[12] {};
	u8 n = legal_actions(t, legal);
	check(has(legal, n, ActionType::Raise), "limit: hay subida");
	check(find_amount(legal, n, ActionType::Raise) == 15, "limit: subida = apuesta viva + ciega");
	check(!has(legal, n, ActionType::AllIn), "limit: no se ofrece all-in");

	// Con el tope alcanzado, ya no se ofrece subir.
	t.raises_this_street = kLimitMaxRaises;
	n = legal_actions(t, legal);
	check(!has(legal, n, ActionType::Raise), "limit: tope de subidas por calle");

	// No-Limit sí ofrece all-in.
	Table nl {};
	eng::Xoroshiro64pp rng2 {33u, 34u};
	start_hand(nl, rng2, 3u, 1000, 5, 10, 0u, PokerVariant::TexasHoldem, BettingStructure::NoLimit);
	n = legal_actions(nl, legal);
	check(has(legal, n, ActionType::AllIn), "no-limit: ofrece all-in");
}

void test_omaha_showdown() {
	// Mesa Omaha preparada a mano: el color de corazones gana al trío de ases.
	Table t {};
	t.seat_count = 2u;
	t.variant = PokerVariant::Omaha;
	t.board_count = kBoardCards;
	t.board[0] = c(Rank::Ace, Suit::Hearts);
	t.board[1] = c(Rank::King, Suit::Hearts);
	t.board[2] = c(Rank::Queen, Suit::Hearts);
	t.board[3] = c(Rank::Jack, Suit::Hearts);
	t.board[4] = c(Rank::Ten, Suit::Hearts);

	for (u8 i = 0u; i < 2u; ++i) {
		t.seats[i].stack = 1000;
		t.seats[i].status = SeatStatus::Active;
		t.seats[i].committed = 100;
	}
	t.seats[0].hole[0] = c(Rank::Ace, Suit::Spades);
	t.seats[0].hole[1] = c(Rank::Ace, Suit::Diamonds);
	t.seats[0].hole[2] = c(Rank::King, Suit::Spades);
	t.seats[0].hole[3] = c(Rank::King, Suit::Diamonds);
	t.seats[1].hole[0] = c(Rank::Nine, Suit::Hearts);
	t.seats[1].hole[1] = c(Rank::Ten, Suit::Hearts);
	t.seats[1].hole[2] = c(Rank::Two, Suit::Spades);
	t.seats[1].hole[3] = c(Rank::Three, Suit::Spades);

	settle_showdown(t);
	check(t.seats[0].stack == 1000 && t.seats[1].stack == 1200,
	      "omaha: gana la escalera de color (asiento 1)");
}

} // namespace

int main() {
	std::printf("eng::cards variants:\n");
	test_omaha_evaluator();
	test_variant_helpers();
	test_variant_dealing();
	test_limit_betting();
	test_omaha_showdown();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards variants (Omaha 2+3, reparto, Limit fijo y tope)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
