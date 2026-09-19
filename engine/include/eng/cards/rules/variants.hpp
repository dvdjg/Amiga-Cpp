#pragma once

/// \file variants.hpp
/// Vocabulario de **variantes de póker** y **estructura de apuestas** que el motor
/// reutiliza sin duplicar reglas ni evaluación.
///
/// - `PokerVariant`: Texas Hold'em (2 hole + 5 comunitarias) y Omaha (4 hole + 5
///   comunitarias, exactamente 2 hole + 3 comunitarias en el showdown).
/// - `BettingStructure`: No-Limit (subida libre) y Limit (tamaño fijo y tope de
///   subidas por calle).
/// - `hole_cards_for`: cuántas cartas recibe cada asiento según la variante.
/// - `limit_bet_size`: tamaño de apuesta fijo de Limit (ciega pequeña preflop/flop,
///   doble en turn/river).
///
/// Verificación: HOST-167. Estado: verificado por test host; consumido por `games/200_holdem` (build → run → analyze OK).

#include <eng/core/types.hpp>

#include <eng/cards/core/types.hpp>

namespace eng::cards {

/// Variante de póker soportada por la mesa.
enum class PokerVariant : u8 {
	TexasHoldem = 0u,
	Omaha,
	Count,
};

/// Estructura de apuestas.
enum class BettingStructure : u8 {
	NoLimit = 0u,
	Limit,
	Count,
};

/// Cartas privadas que reparte cada asiento según la variante.
[[nodiscard]] constexpr u8 hole_cards_for(PokerVariant variant) noexcept {
	return variant == PokerVariant::Omaha ? 4u : kMaxHoleCards;
}

/// Número máximo de subidas por calle en Limit (además de la apuesta inicial).
inline constexpr u8 kLimitMaxRaises = 4u;

/// Tamaño de una apuesta/subida fija en Limit: la ciega pequeña en preflop/flop y el
/// doble (ciega grande) en turn/river. Las ciegas se pasan en `small_blind`/`big_blind`.
[[nodiscard]] constexpr s32 limit_bet_size(Street street, s32 small_blind, s32 big_blind) noexcept {
	if (street == Street::Preflop || street == Street::Flop) {
		return small_blind;
	}
	return big_blind;
}

} // namespace eng::cards
