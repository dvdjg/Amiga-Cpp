#pragma once

/// \file deck.hpp
/// Baraja de 52 cartas y reparto determinista.
///
/// La baraja es un array inline de 52 B más un contador de cartas no repartidas.
/// Se baraja con Fisher-Yates sobre `eng::Xoroshiro64pp` (ya existente en
/// `eng/core/random.hpp`): el mismo PRNG y la misma semilla reproducen la misma
/// secuencia en host y en Amiga, requisito de los tests y del análisis de partidas.
///
/// Operaciones:
/// - `reset()`: vuelve al orden identidad 0..51.
/// - `shuffle(rng)`: permuta las cartas que quedan.
/// - `remove(card)`: saca una carta conocida (hole/board) antes de muestrear.
/// - `deal()`: reparte la siguiente carta desde el final del array vivo.
///
/// Verificación: HOST-161. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/random.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/types.hpp>

namespace eng::cards {

/// Baraja de 52 cartas; `remaining_count` cartas vivas en `cards[0..remaining_count)`.
struct Deck {
	Card cards[kDeckSize] {};
	u8 remaining_count = kDeckSize;

	/// Orden identidad: `cards[i] = i` (carta 0 = 2 de tréboles … carta 51 = As de picas).
	constexpr void reset() noexcept {
		for (u8 i = 0u; i < kDeckSize; ++i) {
			cards[i] = i;
		}
		remaining_count = kDeckSize;
	}

	[[nodiscard]] constexpr Span<Card> live() noexcept {
		return Span<Card> {cards, remaining_count};
	}

	[[nodiscard]] constexpr Span<const Card> live() const noexcept {
		return Span<const Card> {cards, remaining_count};
	}

	/// Baraja las cartas vivas (Fisher-Yates, sin sesgo práctico).
	constexpr void shuffle(eng::Xoroshiro64pp& rng) noexcept {
		eng::shuffle(rng, live());
	}

	/// Saca `card` de la parte viva. Devuelve `false` si ya no estaba.
	constexpr bool remove(Card card) noexcept {
		for (u8 i = 0u; i < remaining_count; ++i) {
			if (cards[i] == card) {
				--remaining_count;
				cards[i] = cards[remaining_count];
				return true;
			}
		}
		return false;
	}

	/// Reparte una carta (desde el final del tramo vivo). `kNoCard` si está vacía.
	constexpr Card deal() noexcept {
		if (remaining_count == 0u) {
			return kNoCard;
		}
		--remaining_count;
		return cards[remaining_count];
	}

	/// Baraja viva tras conocer `known` (hole + board).
	constexpr void shuffle_without(eng::Xoroshiro64pp& rng, Span<const Card> known) noexcept {
		for (usize i = 0u; i < known.size(); ++i) {
			remove(known[i]);
		}
		shuffle(rng);
	}
};

} // namespace eng::cards
