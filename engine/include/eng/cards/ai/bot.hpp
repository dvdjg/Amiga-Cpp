#pragma once

/// \file bot.hpp
/// Política de decisión de un jugador de póker controlado por la CPU y modelo
/// ligero de rival.
///
/// El bot compara su **equity** (Monte Carlo si el perfil lo permite; si no, la
/// heurística preflop/fuerza hecha) con las **pot odds** más un colchón de estilo.
/// El estilo (`BotStyle`) fija con qué frecuencia entra al bote (VPIP), cuánto
/// sube en vez de igualar (agresividad) y cuánto farolea (bluff). No hay `float` ni
/// azar oculto: todo sale del `eng::Xoroshiro64pp` inyectado.
///
/// `OpponentModel` acumula por asiento las acciones observadas (fold/call/raise) y
/// expone frecuencias en por mil, que el bot usa para ajustar el farol y el valor
/// fino. Va dimensionado por el `CardPlan` (`tracked_opponents`).
///
/// Verificación: HOST-165. Estado: verificado por test host; **NO VERIFICADO** en
/// demo/hardware (sin consumidor en `games/` todavía).

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/budget.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/equity.hpp>
#include <eng/cards/rules/hand_rank.hpp>
#include <eng/cards/rules/texas_holdem.hpp>

namespace eng::cards {

/// Estilo de juego. Ortogonal al perfil de memoria: dos bots `N512` pueden tener
/// estilos opuestos para enfrentarlos.
enum class BotStyle : u8 {
	TightPassive = 0u,
	TightAggressive,
	LoosePassive,
	LooseAggressive,
	Balanced,
	Count,
};

/// Parámetros de decisión del bot, en por mil.
struct BotParams {
	u16 vpip_permille = 350u;        ///< probabilidad de entrar al bote con mano marginal
	u16 aggression_permille = 400u;  ///< subir en vez de igualar
	u16 bluff_permille = 90u;        ///< farol con mano débil
	u16 call_margin_permille = 90u;  ///< colchón exigido sobre las pot odds
	u16 mc_samples = 96u;            ///< 0 ⇒ heurística sin Monte Carlo
	u16 bet_permille = 560u;         ///< umbral de mano para apostar sin apuesta previa
	bool use_mc = true;
};

[[nodiscard]] constexpr BotParams bot_params(BotStyle style) noexcept {
	BotParams p {};
	switch (style) {
	case BotStyle::TightPassive:
		p.vpip_permille = 200u;
		p.aggression_permille = 150u;
		p.bluff_permille = 30u;
		p.call_margin_permille = 120u;
		p.bet_permille = 620u;
		break;
	case BotStyle::TightAggressive:
		p.vpip_permille = 250u;
		p.aggression_permille = 560u;
		p.bluff_permille = 80u;
		p.call_margin_permille = 60u;
		p.bet_permille = 520u;
		break;
	case BotStyle::LoosePassive:
		p.vpip_permille = 560u;
		p.aggression_permille = 160u;
		p.bluff_permille = 50u;
		p.call_margin_permille = 150u;
		p.bet_permille = 600u;
		break;
	case BotStyle::LooseAggressive:
		p.vpip_permille = 600u;
		p.aggression_permille = 620u;
		p.bluff_permille = 160u;
		p.call_margin_permille = 50u;
		p.bet_permille = 500u;
		break;
	case BotStyle::Balanced:
		p.vpip_permille = 380u;
		p.aggression_permille = 380u;
		p.bluff_permille = 90u;
		p.call_margin_permille = 90u;
		p.bet_permille = 560u;
		break;
	case BotStyle::Count:
		break;
	}
	return p;
}

/// Fuerza hecha (por mil) a partir de la categoría de la mejor mano actual. Es el
/// *fallback* cuando no hay muestras Monte Carlo.
[[nodiscard]] constexpr u16 category_strength_permille(HandValue value) noexcept {
	switch (hand_category(value)) {
	case HandCategory::HighCard:
		return 280u;
	case HandCategory::Pair:
		return 470u;
	case HandCategory::TwoPair:
		return 620u;
	case HandCategory::Trips:
		return 720u;
	case HandCategory::Straight:
		return 810u;
	case HandCategory::Flush:
		return 860u;
	case HandCategory::FullHouse:
		return 910u;
	case HandCategory::Quads:
		return 970u;
	case HandCategory::StraightFlush:
		return 995u;
	case HandCategory::Count:
		break;
	}
	return 0u;
}

/// Modelo de rival: cuenta acciones observadas por asiento y deduce frecuencias.
struct OpponentModel {
	u16 hands_faced[kMaxSeats] {};
	u16 folds[kMaxSeats] {};
	u16 calls[kMaxSeats] {};
	u16 raises[kMaxSeats] {};
	u8 tracked = kMaxSeats;

	constexpr void reset() noexcept {
		for (u8 i = 0u; i < kMaxSeats; ++i) {
			hands_faced[i] = 0u;
			folds[i] = 0u;
			calls[i] = 0u;
			raises[i] = 0u;
		}
	}

	constexpr void observe(u8 seat, ActionType action) noexcept {
		if (seat >= kMaxSeats) {
			return;
		}
		switch (action) {
		case ActionType::Fold:
			++folds[seat];
			break;
		case ActionType::Call:
			++calls[seat];
			break;
		case ActionType::Raise:
		case ActionType::AllIn:
			++raises[seat];
			break;
		default:
			break;
		}
	}

	[[nodiscard]] constexpr u16 fold_permille(u8 seat) const noexcept {
		const u16 total = static_cast<u16>(folds[seat] + calls[seat] + raises[seat]);
		return total == 0u ? 300u : static_cast<u16>((folds[seat] * kPermilleMax) / total);
	}

	[[nodiscard]] constexpr u16 aggression_permille(u8 seat) const noexcept {
		const u16 total = static_cast<u16>(folds[seat] + calls[seat] + raises[seat]);
		return total == 0u ? 300u : static_cast<u16>((raises[seat] * kPermilleMax) / total);
	}
};

namespace detail {

/// Elige la primera acción del tipo pedido de la lista legal; si no está, `Fallback`.
[[nodiscard]] inline Action pick_action(const Action* legal, u8 count, ActionType type,
                                        Action fallback) noexcept {
	for (u8 i = 0u; i < count; ++i) {
		if (legal[i].type == type) {
			return legal[i];
		}
	}
	return fallback;
}

[[nodiscard]] inline bool has_action(const Action* legal, u8 count, ActionType type) noexcept {
	for (u8 i = 0u; i < count; ++i) {
		if (legal[i].type == type) {
			return true;
		}
	}
	return false;
}

} // namespace detail

/// Fuerza de la mano de `seat` en por mil: Monte Carlo si hay muestras, si no la
/// heurística preflop o la categoría de la mejor mano parcial.
[[nodiscard]] inline u16 hand_strength_permille(const Table& t, u8 seat, const BotParams& params,
                                                eng::Xoroshiro64pp& rng) noexcept {
	const u8 opponents = opponents_in_hand(t, seat);
	if (params.use_mc && params.mc_samples > 0u && t.board_count <= kBoardCards) {
		const eng::Span<const Card> hole {t.seats[seat].hole, kMaxHoleCards};
		const eng::Span<const Card> board {t.board, t.board_count};
		const EquityResult equity = equity_vs_random(hole, board, opponents, params.mc_samples, rng);
		return equity.equity_permille;
	}
	if (t.board_count < 3u) {
		return preflop_strength_permille(t.seats[seat].hole[0], t.seats[seat].hole[1]);
	}
	return category_strength_permille(seat_hand_value(t, seat));
}

/// Decide la acción de `seat`. Respeta siempre la lista de acciones legales.
[[nodiscard]] inline Action decide(const Table& t, u8 seat, const BotParams& params,
                                   const OpponentModel* model,
                                   eng::Xoroshiro64pp& rng) noexcept {
	Action legal[12] {};
	const u8 legal_count = legal_actions(t, legal, 12u);
	if (legal_count == 0u) {
		return Action {ActionType::Check, 0};
	}

	const s32 owe = to_call(t, seat);
	const u16 strength = hand_strength_permille(t, seat, params, rng);

	// Ajuste por modelo de rivales: ante rivales que se retiran mucho, más farol.
	u16 bluff = params.bluff_permille;
	if (model != nullptr) {
		u16 fold_sum = 0u;
		u8 n = 0u;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (i != seat && (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn)) {
				fold_sum = static_cast<u16>(fold_sum + model->fold_permille(i));
				++n;
			}
		}
		if (n > 0u) {
			bluff = static_cast<u16>((bluff + (fold_sum / n)) / 2u);
		}
	}

	if (owe == 0) {
		// Sin apuesta viva: apostar con mano fuerte o farolear; si no, pasar.
		const bool value_bet = strength >= params.bet_permille;
		const bool bluff_now = eng::chance(rng, bluff, kPermilleMax);
		if ((value_bet || bluff_now) && detail::has_action(legal, legal_count, ActionType::Raise)) {
			return detail::pick_action(legal, legal_count, ActionType::Raise, legal[0]);
		}
		if (detail::has_action(legal, legal_count, ActionType::AllIn) && strength >= 920u) {
			return detail::pick_action(legal, legal_count, ActionType::AllIn, legal[0]);
		}
		return detail::pick_action(legal, legal_count, ActionType::Check, legal[0]);
	}

	// Con apuesta viva: comparar fuerza con pot odds + colchón de estilo.
	const u16 odds = pot_odds_permille(owe, t.pot);
	const u32 required = static_cast<u32>(odds) + params.call_margin_permille;
	const bool profitable = strength >= required;
	const bool very_strong = strength >= 780u;
	const bool can_raise = detail::has_action(legal, legal_count, ActionType::Raise);

	if (profitable || very_strong) {
		if (can_raise && (very_strong || eng::chance(rng, params.aggression_permille, kPermilleMax))) {
			return detail::pick_action(legal, legal_count, ActionType::Raise, legal[0]);
		}
		return detail::pick_action(legal, legal_count, ActionType::Call, legal[0]);
	}

	// Mano débil: farol ocasional, si no retirarse.
	if (can_raise && eng::chance(rng, bluff, kPermilleMax)) {
		return detail::pick_action(legal, legal_count, ActionType::Raise, legal[0]);
	}
	if (detail::has_action(legal, legal_count, ActionType::Check)) {
		return detail::pick_action(legal, legal_count, ActionType::Check, legal[0]);
	}
	return detail::pick_action(legal, legal_count, ActionType::Fold, legal[0]);
}

/// Bot con `CardPlan`: deriva los parámetros de estilo y aplica las muestras del
/// perfil de memoria.
[[nodiscard]] inline Action decide_with_plan(const Table& t, u8 seat, BotStyle style,
                                             const CardPlan& plan, const OpponentModel* model,
                                             eng::Xoroshiro64pp& rng) noexcept {
	BotParams params = bot_params(style);
	params.mc_samples = plan.mc_samples;
	params.use_mc = plan.mc_samples > 0u;
	return decide(t, seat, params, model, rng);
}

} // namespace eng::cards
