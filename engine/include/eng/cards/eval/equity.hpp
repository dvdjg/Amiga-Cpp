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
/// El núcleo `equity_core<Ranker>` es **agnóstico de la variante**:
/// - `HoldemRanker` evalúa la mejor de 5 entre 2 privadas + 5 comunitarias;
/// - `OmahaRanker` usa exactamente 2 privadas (de 4) + 3 comunitarias.
/// La **política de reparto del rival** (`OpponentDealer`) llena `Ranker::hole_cards`
/// cartas: `RandomOpponentDealer` al azar; `range.hpp` añade `RangeOpponentDealer`
/// para restringir el rival a las 169 clases de Hold'em. Con comodines, el mazo tiene
/// 54 cartas y el evaluador los sustituye (`hand_rank.hpp`).
///
/// En perfiles sin Monte Carlo (`N20`) se usa `preflop_strength_permille` (o
/// `omaha_preflop_strength_permille`) y la tabla preflop de `range.hpp`.
///
/// Verificación: HOST-191, HOST-193 y HOST-196. Estado: verificado por test host y
/// consumido por `games/200_holdem` (build → run → analyze OK).

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

/// Política por defecto: el rival recibe `out.size()` cartas al azar del mazo vivo.
struct RandomOpponentDealer {
	[[nodiscard]] constexpr bool operator()(Deck& deck, Span<Card> out) const noexcept {
		for (usize i = 0u; i < out.size(); ++i) {
			const Card card = deck.deal();
			if (!card_playable(card)) {
				return false;
			}
			out[i] = card;
		}
		return true;
	}
};

/// Evaluador de Texas Hold'em: mejor de 5 entre 2 privadas + 5 comunitarias.
struct HoldemRanker {
	static constexpr u8 hole_cards = kMaxHoleCards;

	[[nodiscard]] constexpr HandValue operator()(eng::Span<const Card> hole,
	                                             eng::Span<const Card> board) const noexcept {
		Card cards[kMaxHandCards] {};
		u8 n = 0u;
		for (usize i = 0u; i < hole.size() && n < kMaxHandCards; ++i) {
			cards[n++] = hole[i];
		}
		for (usize i = 0u; i < board.size() && n < kMaxHandCards; ++i) {
			cards[n++] = board[i];
		}
		return evaluate_hand(eng::Span<const Card> {cards, n});
	}
};

/// Evaluador de Omaha: exactamente 2 privadas (de 4) + 3 comunitarias (de 5).
struct OmahaRanker {
	static constexpr u8 hole_cards = kSeatCards;

	[[nodiscard]] constexpr HandValue operator()(eng::Span<const Card> hole,
	                                             eng::Span<const Card> board) const noexcept {
		if (hole.size() < 4u || board.size() < kBoardCards) {
			return kHandValueNone;
		}
		Card h[4] {};
		Card b[kBoardCards] {};
		for (u8 i = 0u; i < 4u; ++i) {
			h[i] = hole[i];
		}
		for (u8 i = 0u; i < kBoardCards; ++i) {
			b[i] = board[i];
		}
		return evaluate_omaha(h, b);
	}
};

namespace detail {

/// Núcleo Monte Carlo agnóstico de la variante. `deal_opponent` llena
/// `Ranker::hole_cards` cartas del rival; `with_jokers` usa un mazo de 54.
template <class Ranker, class OpponentDealer>
[[nodiscard]] inline EquityResult equity_core(eng::Span<const Card> hole,
                                              eng::Span<const Card> board, u8 opponents, u16 samples,
                                              eng::Xoroshiro64pp& rng, OpponentDealer deal_opponent,
                                              Ranker ranker, bool with_jokers) noexcept {
	EquityResult result {};
	if (samples == 0u) {
		return result;
	}

	Card known[kMaxHandCards] {};
	u8 known_count = 0u;
	for (usize i = 0u; i < hole.size() && known_count < kMaxHandCards; ++i) {
		if (card_playable(hole[i])) {
			known[known_count++] = hole[i];
		}
	}
	for (usize i = 0u; i < board.size() && known_count < kMaxHandCards; ++i) {
		if (card_playable(board[i])) {
			known[known_count++] = board[i];
		}
	}

	u32 wins = 0u;
	u32 ties = 0u;

	for (u16 sample = 0u; sample < samples; ++sample) {
		Deck deck {};
		deck.reset(with_jokers);
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
			const Card card = deck.deal();
			if (!card_playable(card)) {
				break;
			}
			full_board[board_n++] = card;
		}
		if (board_n < kBoardCards) {
			continue;
		}

		const eng::Span<const Card> board_span {full_board, kBoardCards};
		const HandValue hero = ranker(hole, board_span);

		bool lost = false;
		bool tied = false;
		for (u8 o = 0u; o < opponents; ++o) {
			Card opp_hole[Ranker::hole_cards] {};
			bool dealt = false;
			for (u8 attempt = 0u; attempt < 4u && !dealt; ++attempt) {
				dealt = deal_opponent(deck, eng::Span<Card> {opp_hole, Ranker::hole_cards});
			}
			if (!dealt) {
				lost = true;
				break;
			}
			const HandValue value =
			    ranker(eng::Span<const Card> {opp_hole, Ranker::hole_cards}, board_span);
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

} // namespace detail

/// Equity de `hole` (+ `board`) contra `opponents` manos repartidas por `deal_opponent`
/// (Hold'em). `with_jokers` usa el mazo de 54 cartas.
template <class OpponentDealer>
[[nodiscard]] inline EquityResult equity_vs_dealer(eng::Span<const Card> hole,
                                                   eng::Span<const Card> board, u8 opponents,
                                                   u16 samples, eng::Xoroshiro64pp& rng,
                                                   OpponentDealer deal_opponent,
                                                   bool with_jokers = false) noexcept {
	return detail::equity_core<HoldemRanker>(hole, board, opponents, samples, rng, deal_opponent,
	                                         HoldemRanker {}, with_jokers);
}

/// Equity de Hold'em contra `opponents` manos aleatorias.
[[nodiscard]] inline EquityResult equity_vs_random(eng::Span<const Card> hole,
                                                   eng::Span<const Card> board, u8 opponents,
                                                   u16 samples, eng::Xoroshiro64pp& rng,
                                                   bool with_jokers = false) noexcept {
	return detail::equity_core<HoldemRanker>(hole, board, opponents, samples, rng,
	                                         RandomOpponentDealer {}, HoldemRanker {}, with_jokers);
}

/// Equity de **Omaha** (hole de 4 cartas) contra manos aleatorias.
[[nodiscard]] inline EquityResult equity_vs_random_omaha(eng::Span<const Card> hole,
                                                         eng::Span<const Card> board, u8 opponents,
                                                         u16 samples, eng::Xoroshiro64pp& rng,
                                                         bool with_jokers = false) noexcept {
	return detail::equity_core<OmahaRanker>(hole, board, opponents, samples, rng,
	                                        RandomOpponentDealer {}, OmahaRanker {}, with_jokers);
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
/// Un comodín cuenta como as.
[[nodiscard]] constexpr u16 preflop_strength_permille(Card a, Card b) noexcept {
	if (card_is_joker(a)) {
		a = make_card(Rank::Ace, Suit::Spades);
	}
	if (card_is_joker(b)) {
		b = make_card(Rank::Ace, Suit::Hearts);
	}
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

/// Heurística preflop de Omaha: la mejor pareja de la mano de 4 cartas.
[[nodiscard]] constexpr u16 omaha_preflop_strength_permille(eng::Span<const Card> hole) noexcept {
	const u8 count = static_cast<u8>(hole.size());
	u16 best = 0u;
	for (u8 i = 0u; i < count; ++i) {
		for (u8 j = static_cast<u8>(i + 1u); j < count; ++j) {
			const u16 value = preflop_strength_permille(hole[i], hole[j]);
			if (value > best) {
				best = value;
			}
		}
	}
	return best;
}

} // namespace eng::cards
