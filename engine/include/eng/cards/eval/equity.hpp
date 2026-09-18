#pragma once

/// \file equity.hpp
/// Fuerza de mano y **equity Monte Carlo** contra rivales aleatorios o de un rango.
///
/// El bot decide comparando su equity (probabilidad de ganar + media de empatar)
/// con las *pot odds*. El equity se estima repartiendo `samples` tableros/rivales
/// con `eng::Xoroshiro64pp`: mismo PRNG y misma semilla ⇒ mismos números, en host
/// y en Amiga. Sin `float`: todo en **por mil** (`u16`), que cabe de sobra y evita
/// libgcc (`core/intmath.hpp` hace la división por resta, sin `__divsi3`).
///
/// `equity_vs_dealer` es genérico sobre la **política de reparto del rival**: una
/// lambda o functor que llena sus dos cartas desde el mazo. `RandomOpponentDealer`
/// reparte al azar; `range.hpp` añade `RangeOpponentDealer` para restringir el
/// rival a un conjunto de 169 clases de mano.
///
/// En perfiles sin Monte Carlo (`N20`) se usa `preflop_strength_permille`, una
/// heurística de arranque (pareja, cartas altas, suited, conectores), o la tabla
/// preflop de `range.hpp` cuando el perfil la mantiene en RAM.
///
/// Verificación: HOST-164 y HOST-166. Estado: verificado por test host; **NO
/// VERIFICADO** en demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/random.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/intmath.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/hand_rank.hpp>

namespace eng::cards {

inline constexpr u16 kPermilleMax = 1000u;

/// Resultado de una estimación de equity, en por mil.
struct EquityResult {
	u16 win_permille = 0u;
	u16 tie_permille = 0u;
	u16 equity_permille = 0u; ///< win + tie/2

	[[nodiscard]] constexpr bool strong() const noexcept { return equity_permille >= 500u; }
};

/// Política por defecto: el rival recibe dos cartas al azar del mazo vivo.
struct RandomOpponentDealer {
	[[nodiscard]] constexpr bool operator()(Deck& deck, Card& first, Card& second) const noexcept {
		first = deck.deal();
		second = deck.deal();
		return card_valid(first) && card_valid(second);
	}
};

/// Equity de `hole` (+ `board`) contra `opponents` manos repartidas por `deal_opponent`.
/// Completa el tablero del mazo restante. `samples == 0` devuelve un resultado nulo.
template <class OpponentDealer>
[[nodiscard]] inline EquityResult equity_vs_dealer(eng::Span<const Card> hole,
                                                   eng::Span<const Card> board, u8 opponents,
                                                   u16 samples, eng::Xoroshiro64pp& rng,
                                                   OpponentDealer deal_opponent) noexcept {
	EquityResult result {};
	if (samples == 0u) {
		return result;
	}

	Card known[7] {};
	u8 known_count = 0u;
	for (usize i = 0u; i < hole.size() && known_count < 7u; ++i) {
		known[known_count++] = hole[i];
	}
	for (usize i = 0u; i < board.size() && known_count < 7u; ++i) {
		known[known_count++] = board[i];
	}

	u32 wins = 0u;
	u32 ties = 0u;

	for (u16 sample = 0u; sample < samples; ++sample) {
		Deck deck {};
		deck.reset();
		for (u8 i = 0u; i < known_count; ++i) {
			deck.remove(known[i]);
		}
		deck.shuffle(rng);

		Card full_board[kBoardCards] {};
		u8 board_n = 0u;
		for (usize i = 0u; i < board.size() && board_n < kBoardCards; ++i) {
			full_board[board_n++] = board[i];
		}
		while (board_n < kBoardCards) {
			full_board[board_n++] = deck.deal();
		}

		Card hero_cards[7] {};
		u8 hero_n = 0u;
		for (usize i = 0u; i < hole.size() && hero_n < 2u; ++i) {
			hero_cards[hero_n++] = hole[i];
		}
		for (u8 i = 0u; i < kBoardCards; ++i) {
			hero_cards[hero_n++] = full_board[i];
		}
		const HandValue hero = evaluate_hand(hero_cards, hero_n);

		bool lost = false;
		bool tied = false;
		for (u8 o = 0u; o < opponents; ++o) {
			Card first = kNoCard;
			Card second = kNoCard;
			bool dealt = false;
			for (u8 attempt = 0u; attempt < 4u && !dealt; ++attempt) {
				dealt = deal_opponent(deck, first, second);
			}
			if (!dealt) {
				lost = true;
				break;
			}
			Card opp[7] {};
			opp[0] = first;
			opp[1] = second;
			for (u8 i = 0u; i < kBoardCards; ++i) {
				opp[2u + i] = full_board[i];
			}
			const HandValue value = evaluate_hand(opp, 7u);
			if (value > hero) {
				lost = true;
				break;
			}
			if (value == hero) {
				tied = true;
			}
		}
		if (lost) {
			continue;
		}
		if (tied) {
			++ties;
		} else {
			++wins;
		}
	}

	result.win_permille = permille_u32(wins, samples);
	result.tie_permille = permille_u32(ties, samples);
	result.equity_permille = permille_u32(wins * 2u + ties, static_cast<u32>(samples) * 2u);
	return result;
}

/// Equity contra `opponents` manos aleatorias.
[[nodiscard]] inline EquityResult equity_vs_random(eng::Span<const Card> hole,
                                                   eng::Span<const Card> board, u8 opponents,
                                                   u16 samples, eng::Xoroshiro64pp& rng) noexcept {
	return equity_vs_dealer(hole, board, opponents, samples, rng, RandomOpponentDealer {});
}

/// Probabilidad implícita de la llamada: `to_call / (pot + to_call)` en por mil.
[[nodiscard]] constexpr u16 pot_odds_permille(s32 to_call_amount, s32 pot) noexcept {
	if (to_call_amount <= 0) {
		return 0u;
	}
	const s32 denom = pot + to_call_amount;
	if (denom <= 0) {
		return kPermilleMax;
	}
	return permille_u32(static_cast<u32>(to_call_amount), static_cast<u32>(denom));
}

/// Heurística de fuerza preflop en por mil, sin tablas: pareja, cartas altas,
/// palo igualado y conectores. Aproxima Chen; suficiente para el perfil `N20`.
[[nodiscard]] constexpr u16 preflop_strength_permille(Card a, Card b) noexcept {
	if (!card_valid(a) || !card_valid(b)) {
		return 0u;
	}
	const u8 ra = static_cast<u8>(a >> 2u);
	const u8 rb = static_cast<u8>(b >> 2u);
	const u8 hi = ra > rb ? ra : rb;
	const u8 lo = ra > rb ? rb : ra;
	const bool suited = (a & 0x03u) == (b & 0x03u);
	const u8 gap = static_cast<u8>(hi - lo);

	s32 score = 0;
	if (ra == rb) {
		score = 370 + static_cast<s32>(hi) * 46; // pareja: 22 = 370 … AA = 922
	} else {
		score = 90 + static_cast<s32>(hi) * 28 + static_cast<s32>(lo) * 9;
		if (suited) {
			score += 38;
		}
		if (gap == 1u) {
			score += 8;
		} else if (gap == 2u) {
			score += 4;
		}
	}
	if (score < 0) {
		score = 0;
	}
	if (score > 999) {
		score = 999;
	}
	return static_cast<u16>(score);
}

} // namespace eng::cards
