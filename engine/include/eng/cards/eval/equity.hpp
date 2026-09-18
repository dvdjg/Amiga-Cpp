#pragma once

/// \file equity.hpp
/// Fuerza de mano y **equity Monte Carlo** contra rivales aleatorios.
///
/// El bot decide comparando su equity (probabilidad de ganar + media de empatar)
/// con las *pot odds*. El equity se estima repartiendo `samples` tableros/rivales
/// aleatorios con `eng::Xoroshiro64pp`: mismo PRNG y misma semilla ⇒ mismos
/// números, en host y en Amiga. Sin `float`: todo en **por mil** (`u16`), que cabe
/// de sobra y evita libgcc.
///
/// En perfiles sin Monte Carlo (`N20`) se usa `preflop_strength_permille`, una
/// heurística de arranque (pareja, cartas altas, suited, conectores) que no
/// consume apenas memoria ni tiempo.
///
/// Verificación: HOST-164. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/random.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/deck.hpp>
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

/// Equity de `hole` (+ `board`) contra `opponents` manos aleatorias. Completa el
/// tablero repartiendo del mazo restante. `samples == 0` devuelve un resultado
/// nulo; `opponents == 0` mide manos hechas (triunfa el propio hero).
[[nodiscard]] inline EquityResult equity_vs_random(eng::Span<const Card> hole, eng::Span<const Card> board,
                                                    u8 opponents, u16 samples,
                                                    eng::Xoroshiro64pp& rng) noexcept {
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
			Card opp[7] {};
			opp[0] = deck.deal();
			opp[1] = deck.deal();
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

	result.win_permille = static_cast<u16>((wins * kPermilleMax) / samples);
	result.tie_permille = static_cast<u16>((ties * kPermilleMax) / samples);
	result.equity_permille = static_cast<u16>(((wins * kPermilleMax) + (ties * (kPermilleMax / 2u))) / samples);
	return result;
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
	return static_cast<u16>((to_call_amount * static_cast<s32>(kPermilleMax)) / denom);
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
