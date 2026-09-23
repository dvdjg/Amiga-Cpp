// ============================================================================
// Test HOST-197: Seven-Card Stud (reglas, bring-in, calles y showdown)
// ============================================================================
//
// Valida `eng/cards/rules/seven_stud.hpp`: ante y bring-in, reparto 2+1, avance por
// las cinco calles hasta el showdown, resolucion por retirada y reparto de botes.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/170_cards_stud

#include <cstdio>

#include <eng/core/random.hpp>

#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/seven_stud.hpp>

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

bool has(const Action* legal, u8 n, ActionType type) {
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == type) {
			return true;
		}
	}
	return false;
}

void test_start() {
	StudTable t {};
	eng::Xoroshiro64pp rng {3u, 4u};
	start_stud(t, rng, 4u, 1000, 1, 2, 4, 8, 0u);

	check(t.seat_count == 4u, "stud: 4 asientos");
	check(t.street == StudStreet::Third, "stud: calle 3a");
	bool three_cards = true;
	for (u8 i = 0u; i < 4u; ++i) {
		if (t.seats[i].card_count != 3u) {
			three_cards = false;
		}
	}
	check(three_cards, "stud: 3 cartas por asiento");
	// Ante de 4 jugadores (4) + bring-in (2) = 6.
	check(t.pot == 6, "stud: bote = ante*4 + bring-in");
	check(t.bring_in_seat != kNoSeat, "stud: hay bring-in");
	check(t.to_act != kNoSeat && t.to_act != t.bring_in_seat, "stud: habla tras el bring-in");

	Action legal[12] {};
	const u8 n = stud_legal_actions(t, legal);
	check(has(legal, n, ActionType::Fold), "stud: fold legal");
	check(has(legal, n, ActionType::Call), "stud: call legal");
	check(has(legal, n, ActionType::Raise), "stud: raise legal");
}

void test_full_hand() {
	StudTable t {};
	eng::Xoroshiro64pp rng {9u, 11u};
	start_stud(t, rng, 3u, 1000, 1, 2, 4, 8, 0u);

	u32 guard = 0u;
	while (!t.hand_over && guard < 300u) {
		Action legal[12] {};
		const u8 n = stud_legal_actions(t, legal);
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
		stud_apply_action(t, chosen);
		++guard;
	}
	check(t.hand_over, "stud: la mano termina");
	check(guard < 300u, "stud: sin bucle infinito");
	bool seven = true;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn) {
			if (t.seats[i].card_count != kStudCards) {
				seven = false;
			}
		}
	}
	check(seven, "stud: 7 cartas en el showdown");
	s32 sum = 0;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		sum += t.seats[i].stack;
	}
	check(sum == 3 * 1000, "stud: conservacion de fichas");
}

void test_fold_win() {
	StudTable t {};
	eng::Xoroshiro64pp rng {1u, 2u};
	start_stud(t, rng, 2u, 1000, 1, 2, 4, 8, 0u);
	// Retiran a todos menos uno: el que queda gana el bote.
	while (!t.hand_over) {
		const u8 actor = t.to_act;
		if (actor == kNoSeat) {
			break;
		}
		stud_apply_action(t, Action {ActionType::Fold, 0});
	}
	check(t.hand_over, "stud: retirada cierra la mano");
	check(t.winner_seat != kNoSeat, "stud: hay ganador por retirada");
	s32 sum = 0;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		sum += t.seats[i].stack;
	}
	check(sum == 2000, "stud: fichas conservadas en retirada");
}

void test_street_limits() {
	// En 3a/4a la apuesta es pequena; a partir de 5a, grande.
	StudTable t {};
	eng::Xoroshiro64pp rng {5u, 6u};
	start_stud(t, rng, 3u, 1000, 1, 2, 4, 8, 0u);
	Action legal[12] {};
	u8 n = stud_legal_actions(t, legal);
	s32 raise_amount = -1;
	for (u8 i = 0u; i < n; ++i) {
		if (legal[i].type == ActionType::Raise) {
			raise_amount = legal[i].amount;
		}
	}
	check(raise_amount == 8, "stud: en 3a la subida es la apuesta pequena");
}

} // namespace

int main() {
	std::printf("eng::cards stud:\n");
	test_start();
	test_full_hand();
	test_fold_win();
	test_street_limits();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards stud (bring-in, 5 calles, showdown y retirada)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
