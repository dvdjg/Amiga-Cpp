#pragma once

/// \file seven_stud.hpp
/// Reglas de **Seven-Card Stud Limit** (2..8 jugadores): ante, bring-in, cartas
/// privadas y descubiertas, cinco calles de apuestas (3.ª a 7.ª) y showdown de la
/// mejor de 5 entre 7.
///
/// No hay cartas comunitarias: cada jugador recibe 2 tapadas + 1 descubierta y luego
/// una carta por calle (4.ª–6.ª descubiertas, 7.ª tapada). El **bring-in** lo paga la
/// carta descubierta más baja; el primer turno de cada calle es de la carta
/// descubierta más alta. Apuesta **Limit** (fija con tope de subidas).
///
/// Distribución de las 7 cartas por posición: `[0],[1]` tapadas, `[2]..[5]`
/// descubiertas y `[6]` tapada.
///
/// Verificación: HOST-197. Estado: verificado por test host; consumido por `games/200_holdem` (build → run → analyze OK).

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>

#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/intmath.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/hand_rank.hpp>
#include <eng/cards/rules/texas_holdem.hpp>
#include <eng/cards/rules/variants.hpp>

namespace eng::cards {

inline constexpr u8 kStudMaxSeats = 8u;
inline constexpr u8 kStudCards = 7u;

/// Calle de Seven-Card Stud.
enum class StudStreet : u8 {
	Third = 0u, ///< 2 tapadas + 1 descubierta; bring-in
	Fourth,
	Fifth,
	Sixth,
	Seventh, ///< tapada
	Showdown,
	Count,
};

struct StudSeat {
	s32 stack = 0;
	s32 committed = 0;
	s32 street_bet = 0;
	Card cards[kStudCards] {};
	u8 card_count = 0u;
	SeatStatus status = SeatStatus::Out;
	bool acted = false;
};

struct StudTable {
	StudSeat seats[kStudMaxSeats] {};
	Deck deck {};
	u8 seat_count = 0u;
	u8 to_act = kNoSeat;
	u8 bring_in_seat = kNoSeat; ///< pivote de reparto
	u8 raises_this_street = 0u;
	StudStreet street = StudStreet::Third;
	s32 pot = 0;
	s32 current_bet = 0;
	s32 small_bet = 0;
	s32 big_bet = 0;
	s32 bring_in = 0;
	bool hand_over = false;
	u8 winner_seat = kNoSeat;
};

// ---------------------------------------------------------------------------
// Consultas
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr u8 stud_seats_in_hand(const StudTable& t) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn) {
			++n;
		}
	}
	return n;
}

[[nodiscard]] constexpr u8 stud_seats_can_act(const StudTable& t) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active) {
			++n;
		}
	}
	return n;
}

/// Cartas descubiertas: índices 2..(`card_count`-1), excluyendo la 7.ª (índice 6).
[[nodiscard]] constexpr u8 stud_up_end(const StudSeat& s) noexcept {
	// la 7.ª (índice 6) va tapada.
	if (s.card_count >= kStudCards) {
		return 6u;
	}
	return s.card_count;
}

/// Puntuación de la mejor carta descubierta (rango en bits altos, palo desempata).
[[nodiscard]] constexpr u16 stud_up_score(const StudTable& t, u8 seat) noexcept {
	const StudSeat& s = t.seats[seat];
	const u8 end = stud_up_end(s);
	u16 best = 0u;
	for (u8 i = 2u; i < end; ++i) {
		if (!card_valid(s.cards[i])) {
			continue;
		}
		const u16 key = static_cast<u16>(((s.cards[i] >> 2u) << 2u) | (s.cards[i] & 0x03u));
		if (key > best) {
			best = key;
		}
	}
	return best;
}

/// Mejor mano de 5 del asiento (5..7 cartas).
[[nodiscard]] constexpr HandValue stud_hand_value(const StudTable& t, u8 seat) noexcept {
	const StudSeat& s = t.seats[seat];
	if (s.card_count < 5u) {
		return kHandValueNone;
	}
	return evaluate_hand(eng::Span<const Card> {s.cards, s.card_count});
}

// ---------------------------------------------------------------------------
// Arranque y reparto
// ---------------------------------------------------------------------------

namespace detail {

[[nodiscard]] constexpr u8 stud_ring_add(u8 base, u8 offset, u8 count) noexcept {
	u8 idx = static_cast<u8>(base + offset);
	while (idx >= count) {
		idx = static_cast<u8>(idx - count);
	}
	return idx;
}

constexpr void stud_post(StudTable& t, u8 seat, s32 amount) noexcept {
	StudSeat& s = t.seats[seat];
	s32 pay = amount;
	if (pay > s.stack) {
		pay = s.stack;
	}
	s.stack -= pay;
	s.committed += pay;
	s.street_bet += pay;
	t.pot += pay;
	if (s.stack <= 0) {
		s.status = SeatStatus::AllIn;
	}
}

/// Ante: va al bote y a `committed`, pero **no** cuenta como apuesta de la calle.
constexpr void stud_post_ante(StudTable& t, u8 seat, s32 amount) noexcept {
	StudSeat& s = t.seats[seat];
	s32 pay = amount;
	if (pay > s.stack) {
		pay = s.stack;
	}
	s.stack -= pay;
	s.committed += pay;
	t.pot += pay;
	if (s.stack <= 0) {
		s.status = SeatStatus::AllIn;
	}
}

[[nodiscard]] constexpr bool stud_live(const StudSeat& s) noexcept {
	return s.status == SeatStatus::Active || s.status == SeatStatus::AllIn;
}

/// Reparte una carta más a cada asiento vivo (pivote: `bring_in_seat`).
constexpr void stud_deal_one(StudTable& t) noexcept {
	const u8 pivot = t.bring_in_seat == kNoSeat ? 0u : t.bring_in_seat;
	for (u8 k = 0u; k < t.seat_count; ++k) {
		const u8 seat = stud_ring_add(pivot, static_cast<u8>(1u + k), t.seat_count);
		StudSeat& s = t.seats[seat];
		if (!stud_live(s) || s.card_count >= kStudCards) {
			continue;
		}
		const Card card = t.deck.deal();
		if (!card_playable(card)) {
			continue;
		}
		s.cards[s.card_count] = card;
		++s.card_count;
	}
}

/// Reparte las cartas que falten hasta 7 y resuelve por showdown.
inline void stud_runout(StudTable& t) noexcept;

/// Asiento con la carta descubierta más baja (bring-in); desempata el palo más bajo.
[[nodiscard]] constexpr u8 stud_lowest_up_seat(const StudTable& t) noexcept {
	u8 best = kNoSeat;
	u16 best_key = 0xffffu;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (!stud_live(t.seats[i]) || t.seats[i].card_count < 3u) {
			continue;
		}
		const Card up = t.seats[i].cards[2];
		if (!card_valid(up)) {
			continue;
		}
		const u16 key = static_cast<u16>(((up >> 2u) << 2u) | (up & 0x03u));
		if (key < best_key) {
			best_key = key;
			best = i;
		}
	}
	return best;
}

/// Asiento con la mejor carta descubierta (primer turno post-bring-in).
[[nodiscard]] constexpr u8 stud_highest_up_seat(const StudTable& t) noexcept {
	u8 best = kNoSeat;
	u16 best_key = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status != SeatStatus::Active) {
			continue;
		}
		const u16 key = stud_up_score(t, i);
		if (best == kNoSeat || key > best_key) {
			best_key = key;
			best = i;
		}
	}
	return best;
}

} // namespace detail

/// Arranca una mano de Seven-Card Stud Limit.
inline void start_stud(StudTable& t, eng::Xoroshiro64pp& rng, u8 seat_count, s32 starting_stack,
                       s32 ante, s32 bring_in, s32 small_bet, s32 big_bet, u8 dealer_seat,
                       bool with_jokers = false) noexcept {
	t = StudTable {};
	t.seat_count = seat_count > kStudMaxSeats ? kStudMaxSeats : seat_count;
	t.bring_in = bring_in;
	t.small_bet = small_bet;
	t.big_bet = big_bet;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].status = starting_stack > 0 ? SeatStatus::Active : SeatStatus::Out;
		t.seats[i].stack = starting_stack;
	}
	t.deck.reset(with_jokers);
	t.deck.shuffle(rng);

	t.bring_in_seat = detail::stud_ring_add(0u, dealer_seat, t.seat_count);
	// 2 tapadas + 1 descubierta.
	detail::stud_deal_one(t);
	detail::stud_deal_one(t);
	detail::stud_deal_one(t);

	// Ante al bote (después de repartir, para no marcar AllIn antes de tener cartas).
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active) {
			detail::stud_post_ante(t, i, ante);
		}
	}

	const u8 bring = detail::stud_lowest_up_seat(t);
	t.bring_in_seat = bring == kNoSeat ? 0u : bring;
	if (bring != kNoSeat) {
		detail::stud_post(t, bring, bring_in);
	}
	t.current_bet = small_bet;
	t.to_act = bring == kNoSeat ? detail::stud_highest_up_seat(t)
	                            : detail::stud_ring_add(bring, 1u, t.seat_count);
	t.hand_over = false;
}

// ---------------------------------------------------------------------------
// Showdown
// ---------------------------------------------------------------------------

/// Reparte el bote por niveles de aportación y showdown de la mejor de 5.
inline void settle_stud_showdown(StudTable& t) noexcept {
	s32 contrib[kStudMaxSeats] {};
	s32 won[kStudMaxSeats] {};
	for (u8 i = 0u; i < t.seat_count; ++i) {
		contrib[i] = t.seats[i].committed;
	}
	for (;;) {
		s32 min_c = 0;
		bool found = false;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (contrib[i] > 0 && (!found || contrib[i] < min_c)) {
				min_c = contrib[i];
				found = true;
			}
		}
		if (!found) {
			break;
		}
		s32 pot = 0;
		u8 eligible[kStudMaxSeats] {};
		u8 en = 0u;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (contrib[i] > 0) {
				pot += min_c;
				contrib[i] -= min_c;
				if (detail::stud_live(t.seats[i])) {
					eligible[en++] = i;
				}
			}
		}
		HandValue best = kHandValueNone;
		u8 winners[kStudMaxSeats] {};
		u8 wn = 0u;
		for (u8 e = 0u; e < en; ++e) {
			const HandValue hv = stud_hand_value(t, eligible[e]);
			if (hv > best) {
				best = hv;
				wn = 0u;
				winners[wn++] = eligible[e];
			} else if (hv == best && hv != kHandValueNone) {
				winners[wn++] = eligible[e];
			}
		}
		if (wn > 0u && pot > 0) {
			u32 rem = 0u;
			const s32 share = static_cast<s32>(divmod32(static_cast<u32>(pot), wn, rem));
			for (u8 i = 0u; i < wn; ++i) {
				won[winners[i]] += share;
			}
			if (rem > 0u) {
				won[winners[0]] += static_cast<s32>(rem);
			}
		}
	}
	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].stack += won[i];
	}
	t.pot = 0;
	t.hand_over = true;
	t.to_act = kNoSeat;
}

namespace detail {
inline void stud_runout(StudTable& t) noexcept {
	while (t.deck.remaining_count > 0u) {
		bool need = false;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (stud_live(t.seats[i]) && t.seats[i].card_count < kStudCards) {
				need = true;
			}
		}
		if (!need) {
			break;
		}
		stud_deal_one(t);
	}
	settle_stud_showdown(t);
}
} // namespace detail

// ---------------------------------------------------------------------------
// Acciones
// ---------------------------------------------------------------------------

[[nodiscard]] inline u8 stud_legal_actions(const StudTable& t, eng::Span<Action> out) noexcept {
	if (t.hand_over || t.to_act == kNoSeat) {
		return 0u;
	}
	const StudSeat& s = t.seats[t.to_act];
	const s32 owe = t.current_bet - s.street_bet;
	const s32 bet = (t.street == StudStreet::Third || t.street == StudStreet::Fourth) ? t.small_bet
	                                                                                   : t.big_bet;
	u8 n = 0u;
	const u8 max = static_cast<u8>(out.size());
	auto add = [&](ActionType type, s32 amount) {
		if (n < max) {
			out[n].type = type;
			out[n].amount = amount;
			++n;
		}
	};
	if (owe > 0) {
		add(ActionType::Fold, 0);
		add(ActionType::Call, owe);
	} else {
		add(ActionType::Check, 0);
	}
	if (!s.acted && t.raises_this_street < kLimitMaxRaises && s.stack > owe) {
		s32 target = t.current_bet == 0 ? bet : t.current_bet + bet;
		const s32 max_target = s.street_bet + s.stack;
		if (target > max_target) {
			target = max_target;
		}
		if (target > s.street_bet) {
			add(ActionType::Raise, target);
		}
	}
	return n;
}

/// Aplica una acción y avanza la calle o resuelve la mano.
inline void stud_apply_action(StudTable& t, const Action& action) noexcept {
	if (t.hand_over || t.to_act == kNoSeat) {
		return;
	}
	const u8 seat = t.to_act;
	StudSeat& s = t.seats[seat];
	s.acted = true;
	switch (action.type) {
	case ActionType::Fold:
		s.status = SeatStatus::Folded;
		break;
	case ActionType::Check:
		break;
	case ActionType::Call: {
		s32 owe = t.current_bet - s.street_bet;
		if (owe > s.stack) {
			owe = s.stack;
		}
		if (owe > 0) {
			detail::stud_post(t, seat, owe);
		}
		break;
	}
	case ActionType::Raise:
	case ActionType::AllIn: {
		const s32 old_bet = t.current_bet;
		s32 target = action.amount;
		const s32 max_target = s.street_bet + s.stack;
		if (target > max_target) {
			target = max_target;
		}
		if (target < s.street_bet) {
			target = s.street_bet;
		}
		detail::stud_post(t, seat, target - s.street_bet);
		if (target > old_bet) {
			++t.raises_this_street;
			t.current_bet = target;
			for (u8 i = 0u; i < t.seat_count; ++i) {
				if (i != seat && t.seats[i].status == SeatStatus::Active) {
					t.seats[i].acted = false;
				}
			}
		}
		break;
	}
	default:
		break;
	}

	if (stud_seats_in_hand(t) <= 1u) {
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (detail::stud_live(t.seats[i])) {
				t.winner_seat = i;
				t.seats[i].stack += t.pot;
				break;
			}
		}
		t.pot = 0;
		t.hand_over = true;
		t.to_act = kNoSeat;
		return;
	}
	if (stud_seats_can_act(t) <= 1u) {
		detail::stud_runout(t);
		return;
	}

	// Siguiente asiento que deba actuar.
	u8 next = kNoSeat;
	for (u8 k = 1u; k <= t.seat_count; ++k) {
		const u8 idx = detail::stud_ring_add(seat, k, t.seat_count);
		if (t.seats[idx].status == SeatStatus::Active &&
		    (!t.seats[idx].acted || t.seats[idx].street_bet < t.current_bet)) {
			next = idx;
			break;
		}
	}
	if (next != kNoSeat) {
		t.to_act = next;
		return;
	}

	// Ronda cerrada.
	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].street_bet = 0;
		t.seats[i].acted = false;
	}
	t.current_bet = 0;
	t.raises_this_street = 0u;
	if (t.street == StudStreet::Seventh) {
		t.street = StudStreet::Showdown;
		settle_stud_showdown(t);
		return;
	}
	t.street = static_cast<StudStreet>(static_cast<u8>(t.street) + 1u);
	detail::stud_deal_one(t);
	t.to_act = detail::stud_highest_up_seat(t);
	if (stud_seats_can_act(t) <= 1u) {
		detail::stud_runout(t);
	}
}

} // namespace eng::cards
