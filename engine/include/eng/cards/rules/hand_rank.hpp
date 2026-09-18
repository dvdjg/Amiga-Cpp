#pragma once

/// \file hand_rank.hpp
/// Evaluador de manos de póker: 5 cartas exactas y "la mejor de 5 entre 7"
/// (Texas Hold'em). Devuelve un `HandValue` empaquetado que se compara con `<`/`>`.
///
/// El algoritmo es **de conteo**, no de fuerza bruta: recorre una vez las cartas,
/// llena 13 contadores de rango y 4 contadores/máscaras de palo, y calcula los
/// candidatos de cada categoría para quedarse con el máximo. Esto evita las 21
/// combinaciones de "elegir 5 de 7" y no usa tablas grandes, clave en un 68000.
///
/// Empaquetado (`HandValue` = `u32`):
/// ```text
///   bits 23..20  categoría (0..8)
///   bits 19..16  desempate 1 (mayor)
///   bits 15..12  desempate 2
///   bits 11..8   desempate 3
///   bits  7..4   desempate 4
///   bits  3..0   desempate 5
/// ```
/// Como los rangos caben en 4 bits y las categorías son crecientes, comparar
/// valores enteros da exactamente el orden de manos (sin `float` ni multiplicar).
///
/// Verificación: HOST-162. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/types.hpp>

namespace eng::cards {

namespace detail {

/// Empaqueta categoría + hasta 5 desempates (de mayor a menor).
[[nodiscard]] constexpr HandValue pack_hand(HandCategory category, const u8* ranks, u8 count) noexcept {
	HandValue value = static_cast<HandValue>(category) << 20u;
	for (u8 i = 0u; i < count && i < 5u; ++i) {
		value |= static_cast<HandValue>(ranks[i] & 0x0fu) << static_cast<u32>(16u - 4u * i);
	}
	return value;
}

/// Los `need` rangos más altos presentes en `mask`, de mayor a menor. Devuelve
/// cuántos escribió en `out`.
constexpr u8 top_ranks(u16 mask, u8* out, u8 need) noexcept {
	u8 n = 0u;
	for (s8 rank = 12; rank >= 0 && n < need; --rank) {
		if (((mask >> static_cast<u8>(rank)) & 1u) != 0u) {
			out[n] = static_cast<u8>(rank);
			++n;
		}
	}
	return n;
}

/// Mayor rango de una escalera en `mask`, o `-1`. El as puede ser alto (A-5 = 5 alto).
[[nodiscard]] constexpr s8 straight_high(u16 mask) noexcept {
	for (s8 high = 12; high >= 4; --high) {
		const u16 need = static_cast<u16>(0x1fu << static_cast<u8>(high - 4));
		if ((mask & need) == need) {
			return high;
		}
	}
	// Escalera de as bajo: A,2,3,4,5.
	const u16 wheel = static_cast<u16>((1u << 12u) | (1u << 3u) | (1u << 2u) | (1u << 1u) | (1u << 0u));
	if ((mask & wheel) == wheel) {
		return 3; // cinco alto (índice de rango 3 = "5")
	}
	return -1;
}

} // namespace detail

/// Evalúa una mano de 5 a 7 cartas y devuelve el `HandValue` de la mejor de 5.
/// Cartas inválidas (`kNoCard`) se ignoran; con menos de 5 cartas válidas el
/// resultado es `kHandValueNone`.
[[nodiscard]] constexpr HandValue evaluate_hand(const Card* cards, u8 count) noexcept {
	u8 rank_counts[kRankCount] {};
	u8 suit_counts[kSuitCount] {};
	u16 rank_mask = 0u;
	u16 suit_mask[kSuitCount] {};

	u8 valid = 0u;
	for (u8 i = 0u; i < count; ++i) {
		const Card card = cards[i];
		if (!card_valid(card)) {
			continue;
		}
		const u8 rank = static_cast<u8>(card >> 2u);
		const u8 suit = static_cast<u8>(card & 0x03u);
		++rank_counts[rank];
		++suit_counts[suit];
		rank_mask = static_cast<u16>(rank_mask | (1u << rank));
		suit_mask[suit] = static_cast<u16>(suit_mask[suit] | (1u << rank));
		++valid;
	}
	if (valid < 5u) {
		return kHandValueNone;
	}

	u8 ranks[5] {};

	// Candidato base: carta alta.
	u8 n = detail::top_ranks(rank_mask, ranks, 5u);
	HandValue best = detail::pack_hand(HandCategory::HighCard, ranks, n);

	// Escalera (sin color).
	const s8 straight = detail::straight_high(rank_mask);
	if (straight >= 0) {
		ranks[0] = static_cast<u8>(straight);
		const HandValue candidate = detail::pack_hand(HandCategory::Straight, ranks, 1u);
		if (candidate > best) {
			best = candidate;
		}
	}

	// Color (o escalera de color) del único palo con 5+ cartas.
	s8 flush_suit = -1;
	for (u8 suit = 0u; suit < kSuitCount; ++suit) {
		if (suit_counts[suit] >= 5u) {
			flush_suit = static_cast<s8>(suit);
			break;
		}
	}
	if (flush_suit >= 0) {
		const u16 mask = suit_mask[static_cast<u8>(flush_suit)];
		const s8 sf = detail::straight_high(mask);
		if (sf >= 0) {
			ranks[0] = static_cast<u8>(sf);
			const HandValue candidate = detail::pack_hand(HandCategory::StraightFlush, ranks, 1u);
			if (candidate > best) {
				best = candidate;
			}
		}
		n = detail::top_ranks(mask, ranks, 5u);
		const HandValue candidate = detail::pack_hand(HandCategory::Flush, ranks, n);
		if (candidate > best) {
			best = candidate;
		}
	}

	// Conteo de rangos: póker, tríos y parejas (de mayor a menor).
	s8 quad = -1;
	s8 trip1 = -1;
	s8 trip2 = -1;
	s8 pair1 = -1;
	s8 pair2 = -1;
	u8 pair_count = 0u;
	for (s8 rank = 12; rank >= 0; --rank) {
		const u8 c = rank_counts[static_cast<u8>(rank)];
		if (c >= 4u) {
			if (quad < 0) {
				quad = rank;
			}
		} else if (c == 3u) {
			if (trip1 < 0) {
				trip1 = rank;
			} else if (trip2 < 0) {
				trip2 = rank;
			}
		} else if (c == 2u) {
			if (pair1 < 0) {
				pair1 = rank;
			} else if (pair2 < 0) {
				pair2 = rank;
			}
			if (pair_count < 3u) {
				++pair_count;
			}
		}
	}

	// Póker + kicker.
	if (quad >= 0) {
		ranks[0] = static_cast<u8>(quad);
		u8 rest[1] {};
		detail::top_ranks(static_cast<u16>(rank_mask & ~(1u << static_cast<u8>(quad))), rest, 1u);
		ranks[1] = rest[0];
		const HandValue candidate = detail::pack_hand(HandCategory::Quads, ranks, 2u);
		if (candidate > best) {
			best = candidate;
		}
	}

	// Full: trío + la mejor pareja disponible (segundo trío o pareja).
	if (trip1 >= 0) {
		s8 pair = pair1;
		if (trip2 > pair) {
			pair = trip2;
		}
		if (pair >= 0) {
			ranks[0] = static_cast<u8>(trip1);
			ranks[1] = static_cast<u8>(pair);
			const HandValue candidate = detail::pack_hand(HandCategory::FullHouse, ranks, 2u);
			if (candidate > best) {
				best = candidate;
			}
		}

		// Trío + 2 kickers.
		ranks[0] = static_cast<u8>(trip1);
		u8 rest[2] {};
		detail::top_ranks(static_cast<u16>(rank_mask & ~(1u << static_cast<u8>(trip1))), rest, 2u);
		ranks[1] = rest[0];
		ranks[2] = rest[1];
		const HandValue candidate = detail::pack_hand(HandCategory::Trips, ranks, 3u);
		if (candidate > best) {
			best = candidate;
		}
	}

	// Doble pareja + kicker.
	if (pair_count >= 2u && pair1 >= 0 && pair2 >= 0) {
		const u16 excluded = static_cast<u16>((1u << static_cast<u8>(pair1)) |
		                                      (1u << static_cast<u8>(pair2)));
		ranks[0] = static_cast<u8>(pair1);
		ranks[1] = static_cast<u8>(pair2);
		u8 rest[1] {};
		detail::top_ranks(static_cast<u16>(rank_mask & ~excluded), rest, 1u);
		ranks[2] = rest[0];
		const HandValue candidate = detail::pack_hand(HandCategory::TwoPair, ranks, 3u);
		if (candidate > best) {
			best = candidate;
		}
	}

	// Pareja + 3 kickers.
	if (pair1 >= 0) {
		ranks[0] = static_cast<u8>(pair1);
		u8 rest[3] {};
		detail::top_ranks(static_cast<u16>(rank_mask & ~(1u << static_cast<u8>(pair1))), rest, 3u);
		ranks[1] = rest[0];
		ranks[2] = rest[1];
		ranks[3] = rest[2];
		const HandValue candidate = detail::pack_hand(HandCategory::Pair, ranks, 4u);
		if (candidate > best) {
			best = candidate;
		}
	}

	return best;
}

/// Evalúa exactamente 5 cartas (atajo para tablas y tests).
[[nodiscard]] constexpr HandValue evaluate5(const Card (&cards)[5]) noexcept {
	return evaluate_hand(cards, 5u);
}

/// Mejor mano de 5 entre `count` cartas (hasta 7) pasadas como vista.
[[nodiscard]] constexpr HandValue evaluate_best(eng::Span<const Card> cards) noexcept {
	return evaluate_hand(cards.data(), static_cast<u8>(cards.size()));
}

} // namespace eng::cards
