#pragma once

/// \file session.hpp
/// Simulación de partidas completas entre bots, para **ajustar el nivel desde
/// host** sin emulador: N manos, botón rotando, ciegas fijas y recompra por mano.
///
/// Es la pieza que cierra el ciclo de los juegos de naipes: los tests host juegan
/// miles de manos con semilla fija y comparan net (bb/100), frecuencia de showdown
/// y reparto de botes entre estilos. Todo determinista y sin I/O.
///
/// Verificación: HOST-192. Estado: verificado por test host; consumido por `games/200_holdem` (build → run → analyze OK).

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/ai/bot.hpp>
#include <eng/cards/core/budget.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/texas_holdem.hpp>

namespace eng::cards {

/// Configuración de una sesión de simulación.
struct SessionConfig {
	u8 seats = 6u;
	s32 starting_stack = 1000;
	s32 small_blind = 5;
	s32 big_blind = 10;
	u32 hands = 200u;
	u32 seed = 1u;
	PokerVariant variant = PokerVariant::TexasHoldem;
	bool with_jokers = false;
	BotStyle styles[kMaxSeats] {};
};

/// Resultado agregado de una sesión.
struct SessionStats {
	s32 net[kMaxSeats] {};
	s32 starting_stack = 0;
	u32 hands_played = 0u;
	u32 showdowns = 0u;
	u32 fold_wins = 0u;
	u32 raises = 0u;
	u32 calls = 0u;
	u32 folds = 0u;
	u32 max_actions_in_hand = 0u;

	/// Net del asiento en *big blinds por 100 manos* (×100, entero). Se calcula con
	/// desplazamientos y `div32` (sin `__mulsi3`/`__divsi3`), por si se invoca en el
	/// target 68000.
	[[nodiscard]] constexpr s32 bb_per_100_centi(u8 seat, s32 big_blind) const noexcept {
		if (big_blind <= 0 || hands_played == 0u) {
			return 0;
		}
		const s32 net_value = net[seat];
		const u32 magnitude = net_value < 0 ? static_cast<u32>(-net_value) : static_cast<u32>(net_value);
		// magnitude * 10000 (10000 = 8192 + 1024 + 512 + 256 + 16).
		const u32 scaled = (magnitude << 13u) + (magnitude << 10u) + (magnitude << 9u) +
		                   (magnitude << 8u) + (magnitude << 4u);
		const u32 per_bb = div32(scaled, static_cast<u32>(big_blind));
		const u32 centi = div32(per_bb, hands_played);
		return net_value < 0 ? -static_cast<s32>(centi) : static_cast<s32>(centi);
	}
};

/// Juega `hands` manos con los estilos indicados. `model` (opcional) acumula las
/// acciones observadas. `plan` fija las muestras Monte Carlo por perfil.
/// `preflop_table` (opcional) aporta el equity preflop de las 169 clases y
/// `opponent_range` (opcional) restringe el rival del Monte Carlo.
inline void run_session(const SessionConfig& config, const CardPlan& plan, SessionStats& stats,
                        OpponentModel* model = nullptr,
                        const PreflopTable* preflop_table = nullptr,
                        const HandRange* opponent_range = nullptr) noexcept {
	stats = SessionStats {};
	stats.starting_stack = config.starting_stack;

	const u8 seats = config.seats > kMaxSeats ? kMaxSeats : (config.seats < 2u ? 2u : config.seats);
	eng::Xoroshiro64pp rng {config.seed, config.seed ^ 0x9e3779b9u};
	u8 button = 0u;

	for (u32 hand = 0u; hand < config.hands; ++hand) {
		Table table {};
		start_hand(table, rng, seats, config.starting_stack, config.small_blind, config.big_blind,
		           button, config.variant, BettingStructure::NoLimit, config.with_jokers);
		// Rota el botón sin `%` (evita `__umodsi3` en 68000).
		const u8 next_button = static_cast<u8>(button + 1u);
		button = next_button >= seats ? 0u : next_button;

		u32 actions = 0u;
		while (!table.hand_over && actions < 400u) {
			const u8 actor = table.to_act;
			if (actor == kNoSeat) {
				break;
			}
			const BotStyle style = config.styles[actor];
			const Action action = decide_with_plan(table, actor, style, plan, model, rng,
			                                       preflop_table, opponent_range);

			switch (action.type) {
			case ActionType::Raise:
			case ActionType::AllIn:
				++stats.raises;
				break;
			case ActionType::Call:
				++stats.calls;
				break;
			case ActionType::Fold:
				++stats.folds;
				break;
			default:
				break;
			}
			if (model != nullptr) {
				model->observe(actor, action.type, table.street);
			}
			apply_action(table, action);
			++actions;
		}
		if (actions > stats.max_actions_in_hand) {
			stats.max_actions_in_hand = actions;
		}

		if (table.street == Street::Showdown && seats_in_hand(table) >= 2u) {
			++stats.showdowns;
		} else {
			++stats.fold_wins;
		}

		for (u8 i = 0u; i < seats; ++i) {
			stats.net[i] += table.seats[i].stack - config.starting_stack;
		}
		++stats.hands_played;
	}
}

} // namespace eng::cards
