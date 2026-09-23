#pragma once

/// \file five_draw.hpp
/// Reglas de **Five-Card Draw Limit** (2..8 jugadores) con opción **Deuces Wild**
/// (los doses son comodines): ante, ronda de apuestas, **descarte/robo** de 0..5
/// cartas, segunda ronda y showdown de la mejor mano de 5.
///
/// Reutiliza `evaluate_hand`/`evaluate_deuces_wild` y los mismos tipos (`Action`,
/// `SeatStatus`) que Hold'em/Stud. La apuesta es **Limit** (fija con tope de subidas).
///
/// Verificación: HOST-198. Estado: verificado por test host; consumido por `games/200_holdem` (build → run → analyze OK).

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>

#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/intmath.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/hand_rank.hpp>
#include <eng/cards/rules/texas_holdem.hpp>
#include <eng/cards/rules/variants.hpp>

namespace eng::cards {

inline constexpr u8 kDrawMaxSeats = 8u;
inline constexpr u8 kDrawCards = 5u;

/// Calle de Five-Card Draw.
enum class DrawStreet : u8 {
	Predraw = 0u,
	Draw,
	Postdraw,
	Showdown,
	Count,
};

struct DrawSeat {
	s32 stack = 0;
	s32 committed = 0;
	s32 street_bet = 0;
	Card cards[kDrawCards] {};
	u8 card_count = kDrawCards;
	SeatStatus status = SeatStatus::Out;
	bool acted = false;
};

struct DrawTable {
	DrawSeat seats[kDrawMaxSeats] {};
	Deck deck {};
	u8 seat_count = 0u;
	u8 dealer = 0u;
	u8 to_act = kNoSeat;
	u8 raises_this_street = 0u;
	DrawStreet street = DrawStreet::Predraw;
	s32 pot = 0;
	s32 current_bet = 0;
	s32 small_bet = 0;
	s32 big_bet = 0;
	bool with_jokers = false;
	bool deuces_wild = false;
	bool hand_over = false;
	u8 winner_seat = kNoSeat;
};

// ---------------------------------------------------------------------------
// Consultas
// ---------------------------------------------------------------------------

[[nodiscard]] constexpr u8 draw_seats_in_hand(const DrawTable& t) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn) {
			++n;
		}
	}
	return n;
}

[[nodiscard]] constexpr u8 draw_seats_can_act(const DrawTable& t) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active) {
			++n;
		}
	}
	return n;
}

[[nodiscard]] constexpr HandValue draw_hand_value(const DrawTable& t, u8 seat) noexcept {
	const DrawSeat& s = t.seats[seat];
	if (s.card_count < kDrawCards) {
		return kHandValueNone;
	}
	return t.deuces_wild ? evaluate_deuces_wild(s.cards) : evaluate_hand(s.cards);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace detail {

[[nodiscard]] constexpr u8 draw_ring_add(u8 base, u8 offset, u8 count) noexcept {
	u8 idx = static_cast<u8>(base + offset);
	while (idx >= count) {
		idx = static_cast<u8>(idx - count);
	}
	return idx;
}

constexpr void draw_post(DrawTable& t, u8 seat, s32 amount) noexcept {
	DrawSeat& s = t.seats[seat];
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

[[nodiscard]] constexpr bool draw_live(const DrawSeat& s) noexcept {
	return s.status == SeatStatus::Active || s.status == SeatStatus::AllIn;
}

[[nodiscard]] constexpr u8 draw_first_active(const DrawTable& t) noexcept {
	for (u8 k = 1u; k <= t.seat_count; ++k) {
		const u8 idx = draw_ring_add(t.dealer, k, t.seat_count);
		if (t.seats[idx].status == SeatStatus::Active) {
			return idx;
		}
	}
	return kNoSeat;
}

/// Siguiente asiento que debe actuar en la ronda (o `kNoSeat`).
[[nodiscard]] constexpr u8 draw_next(DrawTable& t, u8 from) noexcept {
	for (u8 k = 1u; k <= t.seat_count; ++k) {
		const u8 idx = draw_ring_add(from, k, t.seat_count);
		if (t.seats[idx].status == SeatStatus::Active &&
		    (!t.seats[idx].acted || t.seats[idx].street_bet < t.current_bet)) {
			return idx;
		}
	}
	return kNoSeat;
}

} // namespace detail

/// Resuelve la mano por showdown (botes laterales por aportación).
inline void settle_draw_showdown(DrawTable& t) noexcept;

// ---------------------------------------------------------------------------
// Arranque y descarte
// ---------------------------------------------------------------------------

/// Arranca una mano de Five-Card Draw. Reparte 5 cartas y paga el ante.
inline void start_draw(DrawTable& t, eng::Xoroshiro64pp& rng, u8 seat_count, s32 starting_stack,
                       s32 ante, s32 small_bet, s32 big_bet, u8 dealer_seat, bool with_jokers = false,
                       bool deuces_wild = false) noexcept {
	t = DrawTable {};
	t.seat_count = seat_count > kDrawMaxSeats ? kDrawMaxSeats : seat_count;
	t.dealer = detail::draw_ring_add(0u, dealer_seat, t.seat_count);
	t.small_bet = small_bet;
	t.big_bet = big_bet;
	t.with_jokers = with_jokers;
	t.deuces_wild = deuces_wild;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].status = starting_stack > 0 ? SeatStatus::Active : SeatStatus::Out;
		t.seats[i].stack = starting_stack;
	}
	t.deck.reset(with_jokers);
	t.deck.shuffle(rng);
	// Reparte 5 cartas a cada asiento, desde la izquierda del repartidor.
	for (u8 round = 0u; round < kDrawCards; ++round) {
		for (u8 k = 0u; k < t.seat_count; ++k) {
			const u8 seat = detail::draw_ring_add(t.dealer, static_cast<u8>(1u + k), t.seat_count);
			DrawSeat& s = t.seats[seat];
			if (s.status != SeatStatus::Active) {
				continue;
			}
			const Card card = t.deck.deal();
			if (card_playable(card)) {
				s.cards[round] = card;
			}
		}
	}
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active) {
			s32 pay = ante;
			if (pay > t.seats[i].stack) {
				pay = t.seats[i].stack;
			}
			t.seats[i].stack -= pay;
			t.seats[i].committed += pay;
			t.pot += pay;
			if (t.seats[i].stack <= 0) {
				t.seats[i].status = SeatStatus::AllIn;
			}
		}
	}
	t.current_bet = 0;
	t.street = DrawStreet::Predraw;
	t.to_act = detail::draw_first_active(t);
	t.hand_over = false;
}

/// Descarta las cartas marcadas en `discard_mask` (bits 0..4) y roba otras tantas.
/// Solo válido en la calle `Draw` para el asiento al turno.
inline void draw_take(DrawTable& t, u8 seat, u8 discard_mask) noexcept {
	if (t.hand_over || t.street != DrawStreet::Draw || seat != t.to_act) {
		return;
	}
	DrawSeat& s = t.seats[seat];
	Card kept[kDrawCards] {};
	u8 kept_n = 0u;
	for (u8 i = 0u; i < kDrawCards; ++i) {
		if (((discard_mask >> i) & 1u) == 0u) {
			kept[kept_n++] = s.cards[i];
		}
	}
	const u8 draw_n = static_cast<u8>(kDrawCards - kept_n);
	for (u8 i = 0u; i < draw_n; ++i) {
		const Card card = t.deck.deal();
		if (card_playable(card)) {
			kept[kept_n++] = card;
		}
	}
	for (u8 i = 0u; i < kDrawCards; ++i) {
		s.cards[i] = kept[i];
	}
	s.card_count = kDrawCards;
	s.acted = true;

	// Avanza el descarte: siguiente asiento activo que aún no haya robado.
	u8 next = kNoSeat;
	for (u8 k = 1u; k <= t.seat_count; ++k) {
		const u8 idx = detail::draw_ring_add(seat, k, t.seat_count);
		if (t.seats[idx].status == SeatStatus::Active && !t.seats[idx].acted) {
			next = idx;
			break;
		}
	}
	if (next == kNoSeat || draw_seats_can_act(t) <= 1u) {
		t.street = DrawStreet::Postdraw;
		t.current_bet = 0;
		t.raises_this_street = 0u;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			t.seats[i].street_bet = 0;
			t.seats[i].acted = false;
		}
		t.to_act = detail::draw_first_active(t);
		if (draw_seats_can_act(t) <= 1u) {
			settle_draw_showdown(t);
		}
	} else {
		t.to_act = next;
	}
}

// ---------------------------------------------------------------------------
// Acciones (Limit)
// ---------------------------------------------------------------------------

[[nodiscard]] inline u8 draw_legal_actions(const DrawTable& t, eng::Span<Action> out) noexcept {
	if (t.hand_over || t.to_act == kNoSeat || t.street == DrawStreet::Draw ||
	    t.street == DrawStreet::Showdown) {
		return 0u;
	}
	const DrawSeat& s = t.seats[t.to_act];
	const s32 owe = t.current_bet - s.street_bet;
	const s32 bet = t.street == DrawStreet::Predraw ? t.small_bet : t.big_bet;
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

/// Aplica una acción de apuestas y avanza la ronda/calle.
inline void draw_apply_action(DrawTable& t, const Action& action) noexcept {
	if (t.hand_over || t.to_act == kNoSeat || t.street == DrawStreet::Draw ||
	    t.street == DrawStreet::Showdown) {
		return;
	}
	const u8 seat = t.to_act;
	DrawSeat& s = t.seats[seat];
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
			detail::draw_post(t, seat, owe);
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
		detail::draw_post(t, seat, target - s.street_bet);
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

	if (draw_seats_in_hand(t) <= 1u) {
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (detail::draw_live(t.seats[i])) {
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

	const u8 next = detail::draw_next(t, seat);
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
	if (t.street == DrawStreet::Predraw) {
		t.street = DrawStreet::Draw;
		t.to_act = detail::draw_first_active(t);
		if (draw_seats_can_act(t) <= 1u) {
			t.street = DrawStreet::Postdraw;
			t.to_act = detail::draw_first_active(t);
			if (draw_seats_can_act(t) <= 1u) {
				settle_draw_showdown(t);
			}
		}
	} else if (t.street == DrawStreet::Postdraw) {
		t.street = DrawStreet::Showdown;
		settle_draw_showdown(t);
	}
}

// ---------------------------------------------------------------------------
// Showdown
// ---------------------------------------------------------------------------

inline void settle_draw_showdown(DrawTable& t) noexcept {
	s32 contrib[kDrawMaxSeats] {};
	s32 won[kDrawMaxSeats] {};
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
		u8 eligible[kDrawMaxSeats] {};
		u8 en = 0u;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (contrib[i] > 0) {
				pot += min_c;
				contrib[i] -= min_c;
				if (detail::draw_live(t.seats[i])) {
					eligible[en++] = i;
				}
			}
		}
		HandValue best = kHandValueNone;
		u8 winners[kDrawMaxSeats] {};
		u8 wn = 0u;
		for (u8 e = 0u; e < en; ++e) {
			const HandValue hv = draw_hand_value(t, eligible[e]);
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

/// Máscara de descarte recomendada (heurística): conserva parejas o mejor, los
/// comodines y (si no hay pareja) las `2 + comodines` cartas más altas. Devuelve los
/// bits a **descartar**.
[[nodiscard]] constexpr u8 recommended_draw_mask(eng::Span<const Card> cards,
                                                 bool deuces_wild) noexcept {
	const u8 count = static_cast<u8>(cards.size());
	u8 rank_count[13] {};
	for (u8 i = 0u; i < count; ++i) {
		if (card_is_joker(cards[i]) ||
		    (deuces_wild && card_valid(cards[i]) &&
		     (cards[i] >> 2u) == static_cast<u8>(Rank::Two))) {
			continue;
		}
		if (card_valid(cards[i])) {
			++rank_count[cards[i] >> 2u];
		}
	}
	bool have_pair = false;
	for (u8 r = 0u; r < 13u; ++r) {
		if (rank_count[r] >= 2u) {
			have_pair = true;
		}
	}
	u8 mask = 0u;
	for (u8 i = 0u; i < count; ++i) {
		const Card card = cards[i];
		const bool wildcard =
		    card_is_joker(card) ||
		    (deuces_wild && card_valid(card) && (card >> 2u) == static_cast<u8>(Rank::Two));
		if (wildcard) {
			continue; // comodín: se queda
		}
		if (have_pair) {
			if (rank_count[card >> 2u] < 2u) {
				mask = static_cast<u8>(mask | (1u << i));
			}
		} else {
			u8 keep = static_cast<u8>(2u + (count > 0u ? 0u : 0u));
			if (keep > 5u) {
				keep = 5u;
			}
			u8 higher = 0u;
			for (u8 j = 0u; j < count; ++j) {
				if (j != i && card_valid(cards[j]) && cards[j] > card) {
					++higher;
				}
			}
			if (higher >= keep) {
				mask = static_cast<u8>(mask | (1u << i));
			}
		}
	}
	return mask;
}

} // namespace eng::cards
