// ============================================================================
// Test HOST-198: Five-Card Draw (+ Deuces Wild)
// ============================================================================
//
// Valida `eng/cards/rules/five_draw.hpp`: ante, ronda de apuestas, descarte/robo,
// segunda ronda, showdown y conservacion; y `evaluate_deuces_wild`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/171_cards_five_draw

#include <cstdio>

#include <eng/core/math/random.hpp>

#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/five_draw.hpp>
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

bool has(const Action* legal, u8 n, ActionType type) {
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == type) {
			return true;
		}
	}
	return false;
}

void test_deuces_wild_eval() {
	// Dos doses + tres ases = poker de ases (un dos hace de As, el otro de kicker).
	const Card quads[5] {c(Rank::Two, Suit::Hearts), c(Rank::Two, Suit::Spades),
	                     c(Rank::Ace, Suit::Hearts), c(Rank::Ace, Suit::Diamonds),
	                     c(Rank::Ace, Suit::Clubs)};
	check(hand_category(evaluate_deuces_wild(quads)) == HandCategory::Quads,
	      "deuces wild: ases + dos doses = poker de ases");
	check(evaluate_deuces_wild(quads) >= evaluate_hand(quads),
	      "deuces wild: no empeora la mano");

	// Un dos completa una escalera.
	const Card straight[5] {c(Rank::Two, Suit::Spades), c(Rank::Five, Suit::Hearts),
	                        c(Rank::Six, Suit::Diamonds), c(Rank::Seven, Suit::Clubs),
	                        c(Rank::Eight, Suit::Spades)};
	check(hand_category(evaluate_deuces_wild(straight)) >= HandCategory::Straight,
	      "deuces wild: el dos completa escalera");

	// Con el rango comodin cambiado (treses wild): un tres hace de As -> poker de ases.
	const Card threes[5] {c(Rank::Three, Suit::Spades), c(Rank::Ace, Suit::Hearts),
	                      c(Rank::Ace, Suit::Diamonds), c(Rank::Ace, Suit::Clubs),
	                      c(Rank::King, Suit::Spades)};
	check(hand_category(evaluate_deuces_wild(threes, Rank::Three)) == HandCategory::Quads,
	      "wild rank: cambiar el rango comodin");
}

void test_start() {
	DrawTable t {};
	eng::Xoroshiro64pp rng {7u, 8u};
	start_draw(t, rng, 4u, 1000, 1, 4, 8, 0u);

	check(t.seat_count == 4u, "draw: 4 asientos");
	check(t.street == DrawStreet::Predraw, "draw: ronda pre-draw");
	bool five = true;
	for (u8 i = 0u; i < 4u; ++i) {
		if (t.seats[i].card_count != kDrawCards) {
			five = false;
		}
	}
	check(five, "draw: 5 cartas por asiento");
	check(t.pot == 4, "draw: bote = ante*4");

	Action legal[12] {};
	const u8 n = draw_legal_actions(t, legal);
	check(has(legal, n, ActionType::Check), "draw: check sin apuesta viva");
	check(has(legal, n, ActionType::Raise), "draw: apuesta inicial");
}

void test_full_hand() {
	DrawTable t {};
	eng::Xoroshiro64pp rng {13u, 17u};
	start_draw(t, rng, 3u, 1000, 1, 4, 8, 0u);

	u32 guard = 0u;
	while (!t.hand_over && guard < 400u) {
		if (t.street == DrawStreet::Draw) {
			const u8 actor = t.to_act;
			const u8 mask = recommended_draw_mask(eng::Span<const Card> {t.seats[actor].cards, kDrawCards}, t.deuces_wild);
			draw_take(t, actor, mask);
			++guard;
			continue;
		}
		Action legal[12] {};
		const u8 n = draw_legal_actions(t, legal);
		if (n == 0u) {
			break;
		}
		Action chosen = legal[0];
		for (u8 i = 0u; i < n; ++i) {
			if (legal[i].type == ActionType::Check) {
				chosen = legal[i];
			}
		}
		for (u8 i = 0u; i < n; ++i) {
			if (legal[i].type == ActionType::Call && !has(legal, n, ActionType::Check)) {
				chosen = legal[i];
			}
		}
		draw_apply_action(t, chosen);
		++guard;
	}
	check(t.hand_over, "draw: la mano termina");
	check(guard < 400u, "draw: sin bucle infinito");
	s32 sum = 0;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		sum += t.seats[i].stack;
	}
	check(sum == 3 * 1000, "draw: conservacion de fichas");
}

void test_deuces_wild_hand() {
	DrawTable t {};
	eng::Xoroshiro64pp rng {23u, 29u};
	start_draw(t, rng, 3u, 1000, 1, 4, 8, 0u, false, true);
	check(t.deuces_wild, "deuces wild: flag activo");

	u32 guard = 0u;
	while (!t.hand_over && guard < 400u) {
		if (t.street == DrawStreet::Draw) {
			const u8 actor = t.to_act;
			draw_take(t, actor, recommended_draw_mask(eng::Span<const Card> {t.seats[actor].cards, kDrawCards}, true));
			++guard;
			continue;
		}
		Action legal[12] {};
		const u8 n = draw_legal_actions(t, legal);
		if (n == 0u) {
			break;
		}
		Action chosen = legal[0];
		for (u8 i = 0u; i < n; ++i) {
			if (legal[i].type == ActionType::Check) {
				chosen = legal[i];
			}
		}
		for (u8 i = 0u; i < n; ++i) {
			if (legal[i].type == ActionType::Call && !has(legal, n, ActionType::Check)) {
				chosen = legal[i];
			}
		}
		draw_apply_action(t, chosen);
		++guard;
	}
	check(t.hand_over, "deuces wild: la mano termina");
	s32 sum = 0;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		sum += t.seats[i].stack;
	}
	check(sum == 3 * 1000, "deuces wild: conservacion de fichas");
}

} // namespace

int main() {
	std::printf("eng::cards five_draw:\n");
	test_deuces_wild_eval();
	test_start();
	test_full_hand();
	test_deuces_wild_hand();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards five_draw (deuces wild, descarte y showdown)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
