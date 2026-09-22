// ============================================================================
// Test HOST-193: rangos de manos y tabla preflop (eng::cards/eval/range.hpp)
// ============================================================================
//
// Valida las 169 clases canonicas (indexado, combinaciones y round-trip), el rango
// como conjunto de clases, el equity contra un rango y la tabla preflop de 169
// valores construida con Monte Carlo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/166_cards_range

#include <cstdio>

#include <eng/cards/ai/bot.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/range.hpp>

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

void test_class_index() {
	bool seen[kPreflopClasses] {};
	u16 combos = 0u;
	for (u8 a = 0u; a < kDeckSize; ++a) {
		for (u8 b = static_cast<u8>(a + 1u); b < kDeckSize; ++b) {
			const u8 index = preflop_class_index(a, b);
			if (index >= kPreflopClasses) {
				check(false, "clases: indice fuera de rango");
				return;
			}
			seen[index] = true;
			++combos;
		}
	}
	u16 distinct = 0u;
	for (u8 i = 0u; i < kPreflopClasses; ++i) {
		if (seen[i]) {
			++distinct;
		}
	}
	check(combos == 1326u, "clases: 1326 combinaciones");
	check(distinct == kPreflopClasses, "clases: 169 clases distintas");

	// Simetria y round-trip por representante.
	bool symmetric = true;
	bool round_trip = true;
	u32 combo_total = 0u;
	for (u8 i = 0u; i < kPreflopClasses; ++i) {
		Card a = kNoCard;
		Card b = kNoCard;
		class_representative(i, a, b);
		if (preflop_class_index(a, b) != i) {
			round_trip = false;
		}
		if (preflop_class_index(b, a) != i) {
			symmetric = false;
		}
		combo_total += class_combo_count(i);
	}
	check(round_trip, "clases: round-trip representante");
	check(symmetric, "clases: indice simetrico");
	check(combo_total == 1326u, "clases: suma de combinaciones = 1326");

	// Pareto: 13 parejas, 78 suited, 78 offsuit.
	check(preflop_class_index(c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts)) < 13u,
	      "clases: pareja en 0..12");
	const u8 suited = preflop_class_index(c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades));
	const u8 offsuit = preflop_class_index(c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Hearts));
	check(suited >= 13u && suited < 91u, "clases: suited en 13..90");
	check(offsuit >= 91u, "clases: offsuit en 91..168");

	// Combo cards validas y distintas.
	bool combos_ok = true;
	for (u8 i = 0u; i < kPreflopClasses; ++i) {
		for (u8 k = 0u; k < class_combo_count(i); ++k) {
			Card a = kNoCard;
			Card b = kNoCard;
			class_combo_cards(i, k, a, b);
			if (!card_valid(a) || !card_valid(b) || a == b || preflop_class_index(a, b) != i) {
				combos_ok = false;
			}
		}
	}
	check(combos_ok, "clases: combinaciones concretas validas");
}

void test_hand_range() {
	HandRange range;
	range.set_all();
	check(range.class_count() == kPreflopClasses, "rango: completo = 169 clases");
	check(range.combo_count() == 1326u, "rango: completo = 1326 combinaciones");

	range.clear();
	check(range.class_count() == 0u && range.combo_count() == 0u, "rango: vacio");

	range.add_hand(c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts));
	check(range.contains(preflop_class_index(c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts))),
	      "rango: contiene AA");
	check(!range.contains_hand(c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades)),
	      "rango: no contiene AKs");
	check(range.combo_count() == 6u, "rango: AA = 6 combinaciones");
}

void test_preflop_table() {
	eng::Xoroshiro64pp rng {2026u, 918u};
	PreflopTable table;
	build_preflop_table(table, rng, 96u);
	check(table.ready, "tabla: construida");
	check(table.samples == 96u, "tabla: muestras");

	const u16 aa = preflop_equity(table, c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts));
	const u16 kk = preflop_equity(table, c(Rank::King, Suit::Spades), c(Rank::King, Suit::Hearts));
	const u16 seven_two = preflop_equity(table, c(Rank::Seven, Suit::Spades), c(Rank::Two, Suit::Hearts));
	const u16 aks = preflop_equity(table, c(Rank::Ace, Suit::Spades), c(Rank::King, Suit::Spades));
	check(aa > kk, "tabla: AA > KK");
	check(kk > seven_two, "tabla: KK > 72o");
	check(aa > 800u, "tabla: AA muy fuerte");
	check(seven_two < 400u, "tabla: 72o debil");
	check(aks > 600u, "tabla: AKs fuerte");

	// Rango por percentil: las 4 mejores clases deben incluir AA.
	HandRange top;
	make_range_by_equity(table, top, 4u);
	check(top.contains(preflop_class_index(c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts))),
	      "rango: top-4 incluye AA");
}

void test_equity_vs_range() {
	const Card aces[2] {c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts)};
	const Card seven_two[2] {c(Rank::Seven, Suit::Spades), c(Rank::Two, Suit::Hearts)};

	HandRange all;
	all.set_all();
	HandRange aces_only;
	aces_only.add(preflop_class_index(c(Rank::Ace, Suit::Spades), c(Rank::Ace, Suit::Hearts)));

	eng::Xoroshiro64pp rng_a {1u, 2u};
	const EquityResult vs_all = equity_vs_range(eng::Span<const Card> {aces, 2u}, eng::Span<const Card> {},
	                                            all, 1u, 300u, rng_a);
	eng::Xoroshiro64pp rng_b {1u, 2u};
	const EquityResult vs_aces = equity_vs_range(eng::Span<const Card> {aces, 2u}, eng::Span<const Card> {},
	                                             aces_only, 1u, 300u, rng_b);
	eng::Xoroshiro64pp rng_c {1u, 2u};
	const EquityResult low_vs_aces = equity_vs_range(eng::Span<const Card> {seven_two, 2u},
	                                                 eng::Span<const Card> {}, aces_only, 1u, 300u, rng_c);

	check(vs_all.equity_permille > vs_aces.equity_permille,
	      "rango: contra rango de AA el equity baja");
	check(low_vs_aces.equity_permille < 300u, "rango: 72o contra solo AA pierde casi siempre");
}

void test_multiway() {
	const u16 hu = 850u;
	const u16 vs2 = multiway_from_heads_up(hu, 2u);
	const u16 vs3 = multiway_from_heads_up(hu, 3u);
	check(vs2 < hu && vs3 < vs2, "multiway: mas rivales, menos equity");
	check(multiway_from_heads_up(1000u, 1u) == 1000u, "multiway: equity perfecto se mantiene");
}

void test_dynamic_range() {
	eng::Xoroshiro64pp table_rng {77u, 88u};
	PreflopTable table;
	build_preflop_table(table, table_rng, 32u);

	eng::Xoroshiro64pp hand_rng {5u, 6u};
	Table t;
	start_hand(t, hand_rng, 3u, 1000, 5, 10, 0u);

	OpponentModel tight;
	OpponentModel loose;
	for (u16 i = 0u; i < 20u; ++i) {
		tight.observe(1u, ActionType::Fold);
		tight.observe(2u, ActionType::Fold);
		loose.observe(1u, ActionType::Raise);
		loose.observe(2u, ActionType::Raise);
	}

	HandRange tight_range;
	HandRange loose_range;
	opponent_range_from_model(tight, t, 0u, table, tight_range);
	opponent_range_from_model(loose, t, 0u, table, loose_range);
	check(tight_range.class_count() > 0u && loose_range.class_count() > 0u,
	      "rango dinamico: rangos no vacios");
	check(tight_range.class_count() < loose_range.class_count(),
	      "rango dinamico: un rival que se retira juega menos manos");

	// Sin tabla no hay ranking: rango completo (equivale a mano aleatoria).
	HandRange all;
	opponent_range_from_model(tight, t, 0u, nullptr, all);
	check(all.class_count() == kPreflopClasses, "rango dinamico: sin tabla = completo");

	// Linea de la calle actual: con el mismo historico, quien sube en la calle juega
	// menos manos (rango mas estrecho) que quien solo pasa.
	OpponentModel line_raiser;
	OpponentModel line_checker;
	for (u16 i = 0u; i < 4u; ++i) {
		line_raiser.observe(1u, ActionType::Call, Street::Preflop);
		line_raiser.observe(2u, ActionType::Call, Street::Preflop);
		line_checker.observe(1u, ActionType::Call, Street::Preflop);
		line_checker.observe(2u, ActionType::Call, Street::Preflop);
	}
	line_raiser.observe(1u, ActionType::Raise, Street::Flop);
	line_raiser.observe(2u, ActionType::Raise, Street::Flop);
	line_checker.observe(1u, ActionType::Check, Street::Flop);
	line_checker.observe(2u, ActionType::Check, Street::Flop);

	HandRange raise_range;
	HandRange check_range;
	opponent_range_from_model(line_raiser, t, 0u, table, raise_range);
	opponent_range_from_model(line_checker, t, 0u, table, check_range);
	check(raise_range.class_count() < check_range.class_count(),
	      "rango dinamico: subir en la calle estrecha el rango");
}

} // namespace

int main() {
	std::printf("eng::cards range:\n");
	test_class_index();
	test_hand_range();
	test_preflop_table();
	test_equity_vs_range();
	test_multiway();
	test_dynamic_range();

	if (g_fail == 0u) {
		std::printf("OK: eng::cards range (169 clases, HandRange, equity vs rango y tabla preflop)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
