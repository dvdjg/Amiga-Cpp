#pragma once

/// \file types.hpp
/// Tipos base de `eng::cards`: palo, rango, carta, mano y calle de apuestas.
///
/// Son **game-agnostic** dentro de los juegos de naipes (póker, blackjack, bridge…)
/// y freestanding. La carta se empaqueta en un byte: `rank << 2 | suit`, con
/// `rank` en 0..12 (2..A) y `suit` en 0..3. De ahí salen índices compactos 0..51
/// para barajar, contar y direccionar tablas sin aritmética de 64 bits.
///
/// Decisiones (68000):
/// - La carta es un `u8`; la baraja de 52 cartas ocupa 52 B y se baraja con XOR
///   y comparaciones, sin `float`.
/// - Las manos viajan como vista (`Span<const Card>`) o como `Hand` de capacidad
///   fija; nunca como punteros sueltos.
/// - El trabajo aritmético usa `cards_int` (`eng::intw`: `s16` en 68000), igual
///   que `board_int` en los juegos de tablero.
///
/// Verificación: HOST-161. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/scalar.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::cards {

/// Entero de trabajo del motor de naipes, elegido en compilación por máquina
/// (`eng::intw`: `s16` en 68000, `s32` en 68020, `int` en host).
using cards_int = eng::intw;

inline constexpr u8 kSuitCount = 4u;
inline constexpr u8 kRankCount = 13u;
inline constexpr u8 kDeckSize = 52u;
inline constexpr u8 kMaxDeckSize = 54u; ///< 52 + 2 comodines
inline constexpr u8 kMaxHoleCards = 2u;  ///< Texas Hold'em
inline constexpr u8 kSeatCards = 4u;     ///< máximo por asiento (Omaha usa 4)
inline constexpr u8 kBoardCards = 5u;
inline constexpr u8 kMaxHandCards = 9u;  ///< 4 privadas (Omaha) + 5 comunitarias

/// Palo de la baraja francesa. El orden no implica fuerza.
enum class Suit : u8 {
	Clubs = 0u,    ///< tréboles
	Diamonds = 1u, ///< diamantes
	Hearts = 2u,   ///< corazones
	Spades = 3u,   ///< picas
};

/// Rango de la carta: 0 = dos, ..., 8 = diez, 9 = J, 10 = Q, 11 = K, 12 = A.
enum class Rank : u8 {
	Two = 0u,
	Three,
	Four,
	Five,
	Six,
	Seven,
	Eight,
	Nine,
	Ten,
	Jack,
	Queen,
	King,
	Ace,
};

/// Carta empaquetada: `rank << 2 | suit`. `kNoCard` es el centinela.
using Card = u8;
inline constexpr Card kNoCard = 0xffu;

/// Primer comodín (joker). Los comodines son `kDeckSize` y `kDeckSize+1`; no tienen
/// rango ni palo y el evaluador los sustituye por la mejor carta posible.
inline constexpr Card kJoker = kDeckSize;

[[nodiscard]] constexpr Card make_card(Rank rank, Suit suit) noexcept {
	return static_cast<Card>((static_cast<u8>(rank) << 2u) | static_cast<u8>(suit));
}

[[nodiscard]] constexpr Rank card_rank(Card card) noexcept {
	return static_cast<Rank>(card >> 2u);
}

[[nodiscard]] constexpr Suit card_suit(Card card) noexcept {
	return static_cast<Suit>(card & 0x03u);
}

[[nodiscard]] constexpr bool card_valid(Card card) noexcept { return card < kDeckSize; }

/// ¿Es un comodín (joker)?
[[nodiscard]] constexpr bool card_is_joker(Card card) noexcept {
	return card >= kDeckSize && card < kMaxDeckSize;
}

/// ¿Es una carta de la baraja ampliada (normal o comodín)?
[[nodiscard]] constexpr bool card_playable(Card card) noexcept {
	return card_valid(card) || card_is_joker(card);
}

/// Calle de apuestas de una mano de póker.
enum class Street : u8 {
	Preflop = 0u,
	Flop,
	Turn,
	River,
	Showdown,
	Count,
};

/// Mano de cartas visible de capacidad fija (2 hole + 5 comunitarias como máximo).
using Hand = eng::util::StaticVector<Card, 7u>;

/// Valor de una mano de 5 cartas empaquetado para comparación numérica:
/// categoría (4 bits) << 20 | hasta 5 desempates de 4 bits (de mayor a menor).
/// Comparar dos valores con `<`/`>` da el ganador; la igualdad es empate.
using HandValue = u32;
inline constexpr HandValue kHandValueNone = 0u;

/// Categorías de mano, de menor a mayor. El valor numérico entra en el empaquetado.
enum class HandCategory : u8 {
	HighCard = 0u,
	Pair,
	TwoPair,
	Trips,
	Straight,
	Flush,
	FullHouse,
	Quads,
	StraightFlush,
	Count,
};

[[nodiscard]] constexpr HandCategory hand_category(HandValue value) noexcept {
	return static_cast<HandCategory>(value >> 20u);
}

[[nodiscard]] constexpr u8 hand_category_index(HandValue value) noexcept {
	return static_cast<u8>(value >> 20u);
}

/// Número de cartas que reparte cada variante de póker (Texas Hold'em).
inline constexpr u8 kHoldemHoleCards = 2u;

/// Nombre legible de la categoría (para UI y explicación). Texto de solo lectura.
[[nodiscard]] constexpr eng::util::StringView hand_category_name(HandCategory category) noexcept {
	switch (category) {
	case HandCategory::HighCard:
		return eng::util::StringView("carta alta");
	case HandCategory::Pair:
		return eng::util::StringView("pareja");
	case HandCategory::TwoPair:
		return eng::util::StringView("doble pareja");
	case HandCategory::Trips:
		return eng::util::StringView("trío");
	case HandCategory::Straight:
		return eng::util::StringView("escalera");
	case HandCategory::Flush:
		return eng::util::StringView("color");
	case HandCategory::FullHouse:
		return eng::util::StringView("full");
	case HandCategory::Quads:
		return eng::util::StringView("póker");
	case HandCategory::StraightFlush:
		return eng::util::StringView("escalera de color");
	case HandCategory::Count:
		break;
	}
	return eng::util::StringView("?");
}

/// Nombre corto de rango (2..A) y de palo, para depuración y UI.
[[nodiscard]] constexpr eng::util::StringView rank_name(Rank rank) noexcept {
	switch (rank) {
	case Rank::Two:
		return eng::util::StringView("2");
	case Rank::Three:
		return eng::util::StringView("3");
	case Rank::Four:
		return eng::util::StringView("4");
	case Rank::Five:
		return eng::util::StringView("5");
	case Rank::Six:
		return eng::util::StringView("6");
	case Rank::Seven:
		return eng::util::StringView("7");
	case Rank::Eight:
		return eng::util::StringView("8");
	case Rank::Nine:
		return eng::util::StringView("9");
	case Rank::Ten:
		return eng::util::StringView("T");
	case Rank::Jack:
		return eng::util::StringView("J");
	case Rank::Queen:
		return eng::util::StringView("Q");
	case Rank::King:
		return eng::util::StringView("K");
	case Rank::Ace:
		return eng::util::StringView("A");
	}
	return eng::util::StringView("?");
}

[[nodiscard]] constexpr eng::util::StringView suit_name(Suit suit) noexcept {
	switch (suit) {
	case Suit::Clubs:
		return eng::util::StringView("C");
	case Suit::Diamonds:
		return eng::util::StringView("D");
	case Suit::Hearts:
		return eng::util::StringView("H");
	case Suit::Spades:
		return eng::util::StringView("S");
	}
	return eng::util::StringView("?");
}

} // namespace eng::cards
