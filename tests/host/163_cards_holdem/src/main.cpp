// ============================================================================
// Test HOST-163: reglas de Texas Hold'em (eng::cards/rules/texas_holdem.hpp)
// ============================================================================
//
// Valida reparto y ciegas, acciones legales, secuencia de calles hasta el
// showdown, resolucion por retirada y reparto de **botes laterales** por niveles
// de aportacion.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/163_cards_holdem

#include <cstdio>

#include <eng/core/random.hpp>

#include <eng/cards/core/types.hpp>
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

bool has(const Action* legal, u8 n, ActionType type) {
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == type) {
			return true;
		}
	}
	return false;
}

void test_start_hand() {
	Table t {};
	eng::Xoroshiro64pp rng {11u, 22u};
	start_hand(t, rng, 3u, 1000, 5, 10, 0u);

	check(t.seat_count == 3u, "reparto: 3 asientos");
	check(t.pot == 15, "reparto: bote = ciegas 5+10");
	check(t.seats[1].street_bet == 5, "reparto: SB aporta 5");
	check(t.seats[2].street_bet == 10, "reparto: BB aporta 10");
	check(t.current_bet == 10, "reparto: apuesta viva = BB");
	check(t.to_act == 0u, "reparto: habla UTG (boton+3 = asiento 0)");
	check(t.seats[0].hole[0] != kNoCard && t.seats[0].hole[1] != kNoCard, "reparto: hole cards");
	check(t.seats[0].hole[0] != t.seats[0].hole[1], "reparto: hole cards distintas");

	Action legal[12] {};
	const u8 n = legal_actions(t, legal, 12u);
	check(has(legal, n, ActionType::Fold), "legal: fold");
	check(has(legal, n, ActionType::Call), "legal: call");
	check(has(legal, n, ActionType::Raise), "legal: raise");
	check(has(legal, n, ActionType::AllIn), "legal: all-in");
	check(!has(legal, n, ActionType::Check), "legal: no hay check con apuesta viva");
}

void test_full_hand_checks() {
	Table t {};
	eng::Xoroshiro64pp rng {7u, 9u};
	start_hand(t, rng, 2u, 1000, 5, 10, 0u);

	u32 guard = 0u;
	while (!t.hand_over && guard < 200u) {
		Action legal[12] {};
		const u8 n = legal_actions(t, legal, 12u);
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
		apply_action(t, chosen);
		++guard;
	}

	check(t.hand_over, "mano: termina");
	check(t.board_count == kBoardCards, "mano: 5 comunitarias en el showdown");
	check(t.seats[0].stack + t.seats[1].stack == 2000, "mano: fichas conservadas");
	check(guard < 200u, "mano: sin bucle infinito");
}

void test_fold_win() {
	Table t {};
	eng::Xoroshiro64pp rng {3u, 4u};
	start_hand(t, rng, 2u, 1000, 5, 10, 0u);
	// Heads-up: habla el boton (SB) primero.
	apply_action(t, Action {ActionType::Fold, 0});
	check(t.hand_over, "retirada: mano cerrada");
	check(t.winner_seat == 1u, "retirada: gana la BB");
	check(t.seats[0].stack == 995, "retirada: SB pierde 5");
	check(t.seats[1].stack == 1005, "retirada: BB gana 5 netos");
}

void test_side_pots() {
	Table t {};
	t.seat_count = 3u;
	t.button = 0u;
	t.board_count = kBoardCards;
	t.board[0] = c(Rank::Two, Suit::Clubs);
	t.board[1] = c(Rank::Seven, Suit::Spades);
	t.board[2] = c(Rank::Nine, Suit::Diamonds);
	t.board[3] = c(Rank::Jack, Suit::Clubs);
	t.board[4] = c(Rank::Three, Suit::Hearts);

	for (u8 i = 0u; i < 3u; ++i) {
		t.seats[i].stack = 1000;
		t.seats[i].status = SeatStatus::Active;
	}
	t.seats[0].committed = 100;
	t.seats[1].committed = 200;
	t.seats[2].committed = 300;
	t.seats[0].hole[0] = c(Rank::Ace, Suit::Hearts);
	t.seats[0].hole[1] = c(Rank::Ace, Suit::Diamonds);
	t.seats[1].hole[0] = c(Rank::King, Suit::Hearts);
	t.seats[1].hole[1] = c(Rank::King, Suit::Diamonds);
	t.seats[2].hole[0] = c(Rank::Queen, Suit::Hearts);
	t.seats[2].hole[1] = c(Rank::Queen, Suit::Diamonds);

	settle_showdown(t);

	check(t.hand_over, "botes: mano resuelta");
	// Capa 1: 300 (AA gana). Capa 2: 200 (KK). Capa 3: 100 (QQ, sin rival).
	check(t.seats[0].stack == 1300, "botes: asiento 0 cobra 300");
	check(t.seats[1].stack == 1200, "botes: asiento 1 cobra 200");
	check(t.seats[2].stack == 1100, "botes: asiento 2 cobra 100");
}

void test_tie_split() {
	Table t {};
	t.seat_count = 2u;
	t.board_count = kBoardCards;
	t.board[0] = c(Rank::Queen, Suit::Clubs);
	t.board[1] = c(Rank::Jack, Suit::Diamonds);
	t.board[2] = c(Rank::Ten, Suit::Spades);
	t.board[3] = c(Rank::Two, Suit::Hearts);
	t.board[4] = c(Rank::Three, Suit::Clubs);
	t.seats[0].stack = 1000;
	t.seats[0].status = SeatStatus::Active;
	t.seats[0].committed = 100;
	t.seats[0].hole[0] = c(Rank::Ace, Suit::Hearts);
	t.seats[0].hole[1] = c(Rank::King, Suit::Hearts);
	t.seats[1].stack = 1000;
	t.seats[1].status = SeatStatus::Active;
	t.seats[1].committed = 100;
	t.seats[1].hole[0] = c(Rank::Ace, Suit::Spades);
	t.seats[1].hole[1] = c(Rank::King, Suit::Spades);

	settle_showdown(t);
	check(t.seats[0].stack == 1100 && t.seats[1].stack == 1100, "empate: bote dividido");
}

} // namespace

int main() {
	std::printf("eng::cards texas_holdem:\n");
	test_start_hand();
	test_full_hand_checks();
	test_fold_win();
	test_side_pots();
	test_tie_split();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards texas_holdem (reparto, calles, retirada y botes laterales)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
