#pragma once

/// \file range.hpp
/// **Rangos de manos** y **tabla preflop de 169 clases** para el motor de naipes.
///
/// En póker, un rival no juega cualquier mano: juega un *rango*. Este módulo
/// representa rangos como conjuntos de las **169 clases canónicas** de mano inicial
/// (13 parejas + 78 suited + 78 offsuit) y permite estimar el equity del héroe
/// contra ese rango (`equity_vs_range`) en vez de contra manos aleatorias.
///
/// También construye una **tabla preflop** de 169 valores en por mil (equity de cada
/// clase contra una mano aleatoria, heads-up). Ocupa 169 × 2 B = 338 B, cabe en
/// cualquier perfil y sustituye la heurística en la decisión preflop. La tabla se
/// calcula una vez con Monte Carlo determinista (`build_preflop_table`) para no
/// incrustar datos opacos; ordenada por equity, genera rangos por percentil
/// (`make_range_by_equity`).
///
/// Nota de muestreo: el dealer de rango elige la clase de forma uniforme y luego una
/// combinación concreta; es una aproximación suficiente para un rival modelado, no
/// un muestreo equiprobable por combinación.
///
/// Verificación: HOST-166. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/random.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>

#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/intmath.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/equity.hpp>
#include <eng/cards/rules/hand_rank.hpp>

namespace eng::cards {

inline constexpr u8 kPreflopClasses = 169u;

/// `triangular[hi] = hi·(hi-1)/2`, con centinela en el índice 13.
inline constexpr u16 kTriangular[14] = {0u, 0u, 1u, 3u, 6u, 10u, 15u, 21u, 28u, 36u, 45u, 55u, 66u, 78u};

/// Índice canónico 0..168 de la clase de la mano `{a, b}`:
/// 0..12 parejas, 13..90 suited, 91..168 offsuit. Simétrico en `a`/`b`.
[[nodiscard]] constexpr u8 preflop_class_index(Card a, Card b) noexcept {
	const u8 ra = static_cast<u8>(a >> 2u);
	const u8 rb = static_cast<u8>(b >> 2u);
	const u8 hi = ra > rb ? ra : rb;
	const u8 lo = ra > rb ? rb : ra;
	if (hi == lo) {
		return hi;
	}
	const u8 tri = static_cast<u8>(kTriangular[hi] + lo);
	const bool suited = (a & 0x03u) == (b & 0x03u);
	return suited ? static_cast<u8>(13u + tri) : static_cast<u8>(91u + tri);
}

/// Clase decodificada: rangos alto/bajo y si es del mismo palo.
struct StartingClass {
	Rank hi = Rank::Two;
	Rank lo = Rank::Two;
	bool suited = false;
};

[[nodiscard]] constexpr StartingClass starting_class(u8 index) noexcept {
	if (index < 13u) {
		return StartingClass {static_cast<Rank>(index), static_cast<Rank>(index), false};
	}
	const bool suited = index < 91u;
	const u8 tri = static_cast<u8>(suited ? (index - 13u) : (index - 91u));
	u8 hi = 1u;
	while (hi < 12u && kTriangular[hi + 1u] <= tri) {
		++hi;
	}
	const u8 lo = static_cast<u8>(tri - kTriangular[hi]);
	return StartingClass {static_cast<Rank>(hi), static_cast<Rank>(lo), suited};
}

/// Número de combinaciones concretas de una clase: pareja 6, suited 4, offsuit 12.
[[nodiscard]] constexpr u8 class_combo_count(u8 index) noexcept {
	const StartingClass sc = starting_class(index);
	if (sc.hi == sc.lo) {
		return 6u;
	}
	return sc.suited ? 4u : 12u;
}

/// Combinación `combo` (0..class_combo_count-1) de una clase, como dos cartas.
constexpr void class_combo_cards(u8 index, u8 combo, Card& first, Card& second) noexcept {
	const StartingClass sc = starting_class(index);
	if (sc.hi == sc.lo) {
		constexpr u8 pairs[6][2] = {{0u, 1u}, {0u, 2u}, {0u, 3u}, {1u, 2u}, {1u, 3u}, {2u, 3u}};
		const u8 c = combo < 6u ? combo : 0u;
		first = make_card(sc.hi, static_cast<Suit>(pairs[c][0]));
		second = make_card(sc.hi, static_cast<Suit>(pairs[c][1]));
		return;
	}
	if (sc.suited) {
		const Suit suit = static_cast<Suit>(combo < 4u ? combo : 0u);
		first = make_card(sc.hi, suit);
		second = make_card(sc.lo, suit);
		return;
	}
	const u8 sa = static_cast<u8>(combo < 12u ? combo / 3u : 0u);
	u8 sb = static_cast<u8>(combo % 3u);
	if (sb >= sa) {
		++sb;
	}
	first = make_card(sc.hi, static_cast<Suit>(sa));
	second = make_card(sc.lo, static_cast<Suit>(sb));
}

/// Una mano representativa de la clase (para construir la tabla preflop).
constexpr void class_representative(u8 index, Card& first, Card& second) noexcept {
	const StartingClass sc = starting_class(index);
	if (sc.hi == sc.lo) {
		first = make_card(sc.hi, Suit::Clubs);
		second = make_card(sc.hi, Suit::Hearts);
	} else if (sc.suited) {
		first = make_card(sc.hi, Suit::Clubs);
		second = make_card(sc.lo, Suit::Clubs);
	} else {
		first = make_card(sc.hi, Suit::Clubs);
		second = make_card(sc.lo, Suit::Diamonds);
	}
}

/// Conjunto de clases de mano inicial (rango de un jugador).
struct HandRange {
	eng::util::BitSet<kPreflopClasses> classes;

	constexpr void clear() noexcept { classes.reset(); }

	/// Rango completo: las 169 clases.
	constexpr void set_all() noexcept {
		classes.reset();
		classes.flip();
	}

	constexpr void add(u8 index) noexcept { classes.set(index); }
	constexpr void remove(u8 index) noexcept { classes.reset(index); }
	[[nodiscard]] constexpr bool contains(u8 index) const noexcept { return classes.test(index); }

	constexpr void add_hand(Card a, Card b) noexcept { classes.set(preflop_class_index(a, b)); }
	[[nodiscard]] constexpr bool contains_hand(Card a, Card b) const noexcept {
		return classes.test(preflop_class_index(a, b));
	}

	[[nodiscard]] constexpr u16 class_count() const noexcept {
		return static_cast<u16>(classes.count());
	}

	/// Combinaciones concretas totales que cubre el rango.
	[[nodiscard]] constexpr u16 combo_count() const noexcept {
		u16 total = 0u;
		for (u8 i = 0u; i < kPreflopClasses; ++i) {
			if (classes.test(i)) {
				total = static_cast<u16>(total + class_combo_count(i));
			}
		}
		return total;
	}
};

/// Reparte la mano del rival desde un `HandRange` (clase uniforme, luego combinación),
/// evitando cartas ya vistas. Falla solo si tras varios intentos no encuentra hueco.
struct RangeOpponentDealer {
	const HandRange* range = nullptr;
	eng::Xoroshiro64pp* rng = nullptr;

	/// Llena 2 cartas del rival (Hold'em) desde el rango. `out.size()` debe ser >= 2.
	[[nodiscard]] constexpr bool operator()(Deck& deck, Span<Card> out) const noexcept {
		if (range == nullptr || rng == nullptr || out.size() < 2u) {
			return false;
		}
		for (u8 attempt = 0u; attempt < 12u; ++attempt) {
			u8 index = static_cast<u8>(rng->next_mod(kPreflopClasses));
			if (!range->contains(index)) {
				continue;
			}
			const u8 combo = static_cast<u8>(rng->next_mod(class_combo_count(index)));
			Card a = kNoCard;
			Card b = kNoCard;
			class_combo_cards(index, combo, a, b);
			if (!deck.contains(a) || !deck.contains(b)) {
				continue;
			}
			deck.remove(a);
			deck.remove(b);
			out[0] = a;
			out[1] = b;
			return true;
		}
		return false;
	}
};

/// Equity del héroe contra `opponents` rivales cuyo rango es `range`.
[[nodiscard]] inline EquityResult equity_vs_range(eng::Span<const Card> hole,
                                                  eng::Span<const Card> board, const HandRange& range,
                                                  u8 opponents, u16 samples,
                                                  eng::Xoroshiro64pp& rng,
                                                  bool with_jokers = false) noexcept {
	RangeOpponentDealer dealer {&range, &rng};
	return equity_vs_dealer(hole, board, opponents, samples, rng, dealer, with_jokers);
}

/// Tabla preflop: equity (por mil, heads-up vs mano aleatoria) de cada una de las 169 clases,
/// más el **orden** de clases por equity (para construir rangos en O(N)).
struct PreflopTable {
	u16 equity_permille[kPreflopClasses] {};
	u8 order[kPreflopClasses] {};
	u16 samples = 0u;
	bool ready = false;
};

/// Construye la tabla con Monte Carlo determinista. `samples_per_class` rollouts por
/// clase (una vez en `init`; en perfiles altos).
inline void build_preflop_table(PreflopTable& table, eng::Xoroshiro64pp& rng,
                                u16 samples_per_class) noexcept {
	table.samples = samples_per_class;
	table.ready = samples_per_class > 0u;
	for (u8 index = 0u; index < kPreflopClasses; ++index) {
		Card a = kNoCard;
		Card b = kNoCard;
		class_representative(index, a, b);
		if (samples_per_class == 0u) {
			table.equity_permille[index] = 0u;
			table.order[index] = index;
			continue;
		}
		const Card hole[2] {a, b};
		const EquityResult eq = equity_vs_random(eng::Span<const Card> {hole, 2u},
		                                         eng::Span<const Card> {}, 1u, samples_per_class, rng);
		table.equity_permille[index] = eq.equity_permille;
	}
	// Orden por equity descendente (selección; una sola vez en `init`).
	bool used[kPreflopClasses] {};
	for (u8 n = 0u; n < kPreflopClasses; ++n) {
		s16 best = -1;
		u16 best_eq = 0u;
		for (u8 i = 0u; i < kPreflopClasses; ++i) {
			if (!used[i] && (best < 0 || table.equity_permille[i] > best_eq)) {
				best = static_cast<s16>(i);
				best_eq = table.equity_permille[i];
			}
		}
		used[static_cast<u8>(best)] = true;
		table.order[n] = static_cast<u8>(best);
	}
}

/// Equity preflop heads-up de la mano `{a, b}` según la tabla (0 si no está lista).
[[nodiscard]] constexpr u16 preflop_equity(const PreflopTable& table, Card a, Card b) noexcept {
	if (!table.ready) {
		return 0u;
	}
	return table.equity_permille[preflop_class_index(a, b)];
}

/// Construye en `out` el rango de las `top_count` clases de mayor equity de la tabla
/// (usa `table.order`, coste O(top_count)). Requiere `table.ready`.
inline void make_range_by_equity(const PreflopTable& table, HandRange& out, u8 top_count) noexcept {
	out.clear();
	if (!table.ready) {
		return;
	}
	const u8 limit = top_count > kPreflopClasses ? kPreflopClasses : top_count;
	for (u8 n = 0u; n < limit; ++n) {
		out.add(table.order[n]);
	}
}

/// Construye el rango del `top_permille` (por mil) de clases de mayor equity.
inline void make_range_by_percentile(const PreflopTable& table, HandRange& out,
                                     u16 top_permille) noexcept {
	const u32 scaled = static_cast<u32>(kPreflopClasses) * top_permille;
	const u32 count = div32(scaled, 1000u);
	const u8 at_least_one = count == 0u ? 1u : static_cast<u8>(count);
	make_range_by_equity(table, out, at_least_one);
}

} // namespace eng::cards
