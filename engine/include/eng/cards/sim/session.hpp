#pragma once

/// \file session.hpp
/// Simulación de partidas completas entre bots, para **ajustar el nivel desde
/// host** sin emulador: N manos, botón rotando, ciegas fijas y recompra por mano.
///
/// Es la pieza que cierra el ciclo de los juegos de naipes: los tests host juegan
/// miles de manos con semilla fija y comparan net (bb/100), frecuencia de showdown
/// y reparto de botes entre estilos. Todo determinista y sin I/O.
///
/// Verificación: HOST-165. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

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

	/// Net del asiento en *big blinds por 100 manos* (×100, entero).
	[[nodiscard]] constexpr s32 bb_per_100_centi(u8 seat, s32 big_blind) const noexcept {
		if (big_blind <= 0 || hands_played == 0u) {
			return 0;
		}
		return (net[seat] * 10000) / (big_blind * static_cast<s32>(hands_played));
	}
};

/// Juega `hands` manos con los estilos indicados. `model` (opcional) acumula las
/// acciones observadas. `plan` fija las muestras Monte Carlo por perfil.
inline void run_session(const SessionConfig& config, const CardPlan& plan, SessionStats& stats,
                        OpponentModel* model = nullptr) noexcept {
	stats = SessionStats {};
	stats.starting_stack = config.starting_stack;

	const u8 seats = config.seats > kMaxSeats ? kMaxSeats : (config.seats < 2u ? 2u : config.seats);
	eng::Xoroshiro64pp rng {config.seed, config.seed ^ 0x9e3779b9u};

	for (u32 hand = 0u; hand < config.hands; ++hand) {
		Table table {};
		const u8 button = static_cast<u8>(hand % seats);
		start_hand(table, rng, seats, config.starting_stack, config.small_blind, config.big_blind, button);

		u32 actions = 0u;
		while (!table.hand_over && actions < 400u) {
			const u8 actor = table.to_act;
			if (actor == kNoSeat) {
				break;
			}
			const BotStyle style = config.styles[actor];
			const Action action = decide_with_plan(table, actor, style, plan, model, rng);

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
				model->observe(actor, action.type);
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
