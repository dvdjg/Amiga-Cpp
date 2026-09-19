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
/// Verificación: HOST-165. Estado: verificado por test host; consumido por `games/200_holdem` (build → run → analyze OK).

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/budget.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/eval/equity.hpp>
#include <eng/cards/eval/range.hpp>
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

/// Modelo de rival: cuenta acciones observadas por asiento (histórico completo y
/// **calle actual**) y deduce frecuencias. La agresión de la calle actual es la que
/// delata la fuerza de la línea de apuesta (un rival que sube juega menos manos).
struct OpponentModel {
	u16 hands_faced[kMaxSeats] {};
	u16 folds[kMaxSeats] {};
	u16 calls[kMaxSeats] {};
	u16 raises[kMaxSeats] {};
	u16 street_raises[kMaxSeats] {};
	u16 street_passive[kMaxSeats] {}; ///< calls + checks de la calle actual
	u8 current_street = 0xffu;
	u8 tracked = kMaxSeats;

	constexpr void reset() noexcept {
		for (u8 i = 0u; i < kMaxSeats; ++i) {
			hands_faced[i] = 0u;
			folds[i] = 0u;
			calls[i] = 0u;
			raises[i] = 0u;
			street_raises[i] = 0u;
			street_passive[i] = 0u;
		}
		current_street = 0xffu;
	}

	constexpr void reset_street(Street street) noexcept {
		for (u8 i = 0u; i < kMaxSeats; ++i) {
			street_raises[i] = 0u;
			street_passive[i] = 0u;
		}
		current_street = static_cast<u8>(street);
	}

	constexpr void observe(u8 seat, ActionType action, Street street = Street::Preflop) noexcept {
		if (seat >= kMaxSeats) {
			return;
		}
		if (static_cast<u8>(street) != current_street) {
			reset_street(street);
		}
		switch (action) {
		case ActionType::Fold:
			++folds[seat];
			++street_passive[seat]; // retirarse no sube
			break;
		case ActionType::Call:
			++calls[seat];
			++street_passive[seat];
			break;
		case ActionType::Check:
			++street_passive[seat];
			break;
		case ActionType::Raise:
		case ActionType::AllIn:
			++raises[seat];
			++street_raises[seat];
			break;
		default:
			break;
		}
	}

	[[nodiscard]] constexpr u16 fold_permille(u8 seat) const noexcept {
		const u16 total =
		    static_cast<u16>(folds[seat] + calls[seat] + raises[seat]);
		return total == 0u ? 300u : permille_u32(folds[seat], total);
	}

	[[nodiscard]] constexpr u16 aggression_permille(u8 seat) const noexcept {
		const u16 total =
		    static_cast<u16>(folds[seat] + calls[seat] + raises[seat]);
		return total == 0u ? 300u : permille_u32(raises[seat], total);
	}

	/// Agresión de la **calle actual**: proporción de subidas frente a acciones
	/// pasivas (call/check/fold). Sin datos, 300 ‰ (neutro).
	[[nodiscard]] constexpr u16 line_aggression_permille(u8 seat) const noexcept {
		const u16 total = static_cast<u16>(street_raises[seat] + street_passive[seat]);
		return total == 0u ? 300u : permille_u32(street_raises[seat], total);
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
/// tabla preflop, la heurística preflop o la categoría de la mejor mano parcial.
/// `table` (opcional) aporta el equity preflop de las 169 clases; `opponent_range`
/// (opcional) restringe al rival en el Monte Carlo.
[[nodiscard]] inline u16 hand_strength_permille(const Table& t, u8 seat, const BotParams& params,
                                                eng::Xoroshiro64pp& rng,
                                                const PreflopTable* table = nullptr,
                                                const HandRange* opponent_range = nullptr) noexcept {
	const u8 opponents = opponents_in_hand(t, seat);
	const bool omaha = t.variant == PokerVariant::Omaha;
	const u8 hole_count = omaha ? kSeatCards : kMaxHoleCards;
	if (params.use_mc && params.mc_samples > 0u && t.board_count <= kBoardCards) {
		const eng::Span<const Card> hole {t.seats[seat].hole, hole_count};
		const eng::Span<const Card> board {t.board, t.board_count};
		const EquityResult equity =
		    omaha ? equity_vs_random_omaha(hole, board, opponents, params.mc_samples, rng,
		                                   t.with_jokers)
		          : ((opponent_range != nullptr)
		                 ? equity_vs_range(hole, board, *opponent_range, opponents,
		                                   params.mc_samples, rng, t.with_jokers)
		                 : equity_vs_random(hole, board, opponents, params.mc_samples, rng,
		                                    t.with_jokers));
		return equity.equity_permille;
	}
	if (t.board_count < 3u) {
		if (omaha) {
			// Omaha: heurística sobre la mejor pareja de las 4 privadas; la tabla de
			// 169 clases es de Hold'em y no aplica.
			return omaha_preflop_strength_permille(t.seats[seat].hole, hole_count);
		}
		if (table != nullptr && table->ready) {
			const u16 hu = preflop_equity(*table, t.seats[seat].hole[0], t.seats[seat].hole[1]);
			return multiway_from_heads_up(hu, opponents);
		}
		return preflop_strength_permille(t.seats[seat].hole[0], t.seats[seat].hole[1]);
	}
	return category_strength_permille(seat_hand_value(t, seat));
}

/// Rango de rival derivado del modelo observado: un rival que se retira mucho juega
/// menos manos (rango estrecho) y uno agresivo/pegajoso juega más. Requiere la tabla
/// preflop lista para ordenar por equity; si no lo está, deja el rango completo.
/// Coste O(N) sobre `table->order`.
inline void opponent_range_from_model(const OpponentModel& model, const Table& t, u8 hero_seat,
                                      const PreflopTable* table, HandRange& out) noexcept {
	if (table == nullptr || !table->ready) {
		out.set_all();
		return;
	}
	u16 fold_sum = 0u;
	u16 aggr_sum = 0u;
	u16 line_sum = 0u;
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		const SeatStatus st = t.seats[i].status;
		if (i != hero_seat && (st == SeatStatus::Active || st == SeatStatus::AllIn)) {
			fold_sum = static_cast<u16>(fold_sum + model.fold_permille(i));
			aggr_sum = static_cast<u16>(aggr_sum + model.aggression_permille(i));
			line_sum = static_cast<u16>(line_sum + model.line_aggression_permille(i));
			++n;
		}
	}
	if (n == 0u) {
		out.set_all();
		return;
	}
	const u16 fold = static_cast<u16>(div32(fold_sum, n));
	const u16 aggr = static_cast<u16>(div32(aggr_sum, n));
	const u16 line = static_cast<u16>(div32(line_sum, n));
	// 400 ‰ por defecto; retirarse mucho estrecha el rango y la agresividad sostenida
	// lo ensancha. La **línea de la calle actual** manda: si el rival sube, juega menos
	// manos y mejores, así que estrecha el rango.
	s32 wide = 400 + (300 - static_cast<s32>(fold)) + (static_cast<s32>(aggr) - 300) / 2 -
	           (static_cast<s32>(line) - 300) / 2;
	if (wide < 80) {
		wide = 80;
	}
	if (wide > 850) {
		wide = 850;
	}
	make_range_by_percentile(*table, out, static_cast<u16>(wide));
}

/// Decide la acción de `seat`. Respeta siempre la lista de acciones legales.
[[nodiscard]] inline Action decide(const Table& t, u8 seat, const BotParams& params,
                                   const OpponentModel* model, eng::Xoroshiro64pp& rng,
                                   const PreflopTable* table = nullptr,
                                   const HandRange* opponent_range = nullptr) noexcept {
	Action legal[12] {};
	const u8 legal_count = legal_actions(t, legal, 12u);
	if (legal_count == 0u) {
		return Action {ActionType::Check, 0};
	}

	const s32 owe = to_call(t, seat);

	// Rango dinámico: si hay modelo de rival y no se fijó un rango explícito, se
	// deriva de sus frecuencias (fold/call/raise) observadas.
	HandRange dynamic_range;
	const HandRange* effective_range = opponent_range;
	if (effective_range == nullptr && model != nullptr && params.use_mc && params.mc_samples > 0u) {
		opponent_range_from_model(*model, t, seat, table, dynamic_range);
		effective_range = &dynamic_range;
	}

	const u16 strength = hand_strength_permille(t, seat, params, rng, table, effective_range);

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
			bluff = static_cast<u16>((bluff + div32(fold_sum, n)) / 2u);
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
/// perfil de memoria. `table`/`range` (opcionales) activan la tabla preflop y el
/// modelo de rango del rival.
[[nodiscard]] inline Action decide_with_plan(const Table& t, u8 seat, BotStyle style,
                                             const CardPlan& plan, const OpponentModel* model,
                                             eng::Xoroshiro64pp& rng,
                                             const PreflopTable* table = nullptr,
                                             const HandRange* opponent_range = nullptr) noexcept {
	BotParams params = bot_params(style);
	params.mc_samples = plan.mc_samples;
	params.use_mc = plan.mc_samples > 0u;
	return decide(t, seat, params, model, rng, table, opponent_range);
}

} // namespace eng::cards
