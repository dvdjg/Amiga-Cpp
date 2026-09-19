#pragma once

/// \file texas_holdem.hpp
/// Reglas de **Texas Hold'em No-Limit** (2..10 jugadores): reparto, ciegas,
/// rondas de apuestas (preflop/flop/turn/river), acciones legales, botes laterales
/// (*side pots*) y showdown.
///
/// El estado `Table` es un struct de campos públicos y tamaño fijo (sin heap): se
/// instancia una vez y vive durante la partida. Todas las funciones son libres y
/// deterministas; el azar entra solo por un `eng::Xoroshiro64pp` inyectado, de modo
/// que una semilla reproduce la partida completa (requisito de los tests host y del
/// análisis de nivel).
///
/// Modelo de apuestas:
/// - Cada asiento lleva `committed` (total aportado en la mano, base de los botes
///   laterales) y `street_bet` (aportado en la ronda actual).
/// - `current_bet` es la apuesta viva y `min_raise` la subida mínima legal.
/// - Un `Raise` lleva como `amount` la apuesta **total objetivo** de la ronda.
/// - Una subida corta *all-in* no reabre la acción: los que ya actuaron solo
///   pueden igualar o retirarse (se conserva `acted`).
///
/// Verificación: HOST-190 (reglas, acción legal, botes laterales y showdown).
/// Estado: verificado por test host y consumido por `games/200_holdem`
/// (build → run → analyze OK).

#include <eng/core/types.hpp>

#include <eng/cards/core/deck.hpp>
#include <eng/cards/core/intmath.hpp>
#include <eng/cards/core/types.hpp>
#include <eng/cards/rules/hand_rank.hpp>
#include <eng/cards/rules/variants.hpp>

namespace eng::cards {

inline constexpr u8 kMaxSeats = 10u;
inline constexpr u8 kNoSeat = 0xffu;
inline constexpr s32 kNoBet = -1;

/// Estado de un asiento durante la mano.
enum class SeatStatus : u8 {
	Out = 0u,   ///< sin fichas o no participa
	Active,     ///< puede actuar
	Folded,     ///< se retiró
	AllIn,      ///< metió todas sus fichas
};

/// Acción de apuestas.
enum class ActionType : u8 {
	Fold = 0u,
	Check,
	Call,
	Raise,
	AllIn,
};

/// Acción concreta. En `Raise`, `amount` es la apuesta total objetivo de la ronda.
struct Action {
	ActionType type = ActionType::Fold;
	s32 amount = 0;
};

/// Asiento de la mesa. 16 B; el estado completo de 10 asientos cabe en ~160 B.
struct Seat {
	s32 stack = 0;
	s32 committed = 0;
	s32 street_bet = 0;
	Card hole[kSeatCards] {kNoCard, kNoCard, kNoCard, kNoCard};
	SeatStatus status = SeatStatus::Out;
	bool acted = false;
};

/// Mesa completa de una mano de póker. Tamaño fijo y sin punteros.
struct Table {
	Seat seats[kMaxSeats] {};
	Card board[kBoardCards] {};
	Deck deck {};
	u8 seat_count = 0u;
	u8 board_count = 0u;
	u8 button = 0u;
	u8 to_act = kNoSeat;
	u8 last_aggressor = kNoSeat;
	u8 raises_this_street = 0u; ///< subidas en la calle (tope en Limit)
	Street street = Street::Preflop;
	PokerVariant variant = PokerVariant::TexasHoldem;
	BettingStructure structure = BettingStructure::NoLimit;
	s32 pot = 0;
	s32 current_bet = 0;
	s32 min_raise = 0;
	s32 small_blind = 0;
	s32 big_blind = 0;
	bool hand_over = false;
	bool with_jokers = false; ///< mazo de 54 cartas con 2 comodines
	u8 winner_seat = kNoSeat; ///< ganador único al retirarse todos (si procede)
};

// ---------------------------------------------------------------------------
// Consultas
// ---------------------------------------------------------------------------

/// Nº de asientos que siguen en la mano (activos o all-in), es decir no retirados.
[[nodiscard]] constexpr u8 seats_in_hand(const Table& t) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn) {
			++n;
		}
	}
	return n;
}

/// Nº de asientos que aún pueden actuar (no retirados y no all-in).
[[nodiscard]] constexpr u8 seats_can_act(const Table& t) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (t.seats[i].status == SeatStatus::Active) {
			++n;
		}
	}
	return n;
}

/// Rivales que siguen en la mano respecto a `seat` (para el equity Monte Carlo).
[[nodiscard]] constexpr u8 opponents_in_hand(const Table& t, u8 seat) noexcept {
	u8 n = 0u;
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (i != seat && (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn)) {
			++n;
		}
	}
	return n;
}

/// Fichas que debe poner `seat` para igualar la apuesta viva.
[[nodiscard]] constexpr s32 to_call(const Table& t, u8 seat) noexcept {
	const s32 owe = t.current_bet - t.seats[seat].street_bet;
	return owe > 0 ? owe : 0;
}

/// Valor de la mejor mano de 5 de `seat` (requiere 5 comunitarias). En Omaha usa
/// exactamente 2 de las 4 privadas y 3 del tablero (`evaluate_omaha`).
[[nodiscard]] constexpr HandValue seat_hand_value(const Table& t, u8 seat) noexcept {
	if (t.board_count < kBoardCards) {
		return kHandValueNone;
	}
	if (t.variant == PokerVariant::Omaha) {
		return evaluate_omaha(t.seats[seat].hole, t.board);
	}
	Card cards[7] {};
	cards[0] = t.seats[seat].hole[0];
	cards[1] = t.seats[seat].hole[1];
	for (u8 i = 0u; i < kBoardCards; ++i) {
		cards[2u + i] = t.board[i];
	}
	return evaluate_hand(cards, 7u);
}

// ---------------------------------------------------------------------------
// Arranque de mano
// ---------------------------------------------------------------------------

namespace detail {

/// Avanza un asiento dentro del anillo `0..count-1` sin `%` (el 68000 no tiene
/// módulo de 32 bits nativo: `%` con divisor runtime acaba en `__umodsi3`).
[[nodiscard]] constexpr u8 ring_next(u8 seat, u8 count) noexcept {
	const u8 n = static_cast<u8>(seat + 1u);
	return n >= count ? 0u : n;
}

/// `(base + offset) % count` para valores que no dan más de una vuelta al anillo.
[[nodiscard]] constexpr u8 ring_add(u8 base, u8 offset, u8 count) noexcept {
	u8 idx = static_cast<u8>(base + offset);
	while (idx >= count) {
		idx = static_cast<u8>(idx - count);
	}
	return idx;
}

[[nodiscard]] constexpr u8 next_seat(u8 seat, u8 count) noexcept {
	return ring_next(seat, count);
}

constexpr void post(Table& t, u8 seat, s32 amount) noexcept {
	Seat& s = t.seats[seat];
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

/// ¿Este asiento debe actuar todavía en la ronda?
[[nodiscard]] constexpr bool needs_action(const Table& t, u8 seat) noexcept {
	const Seat& s = t.seats[seat];
	return s.status == SeatStatus::Active && (!s.acted || s.street_bet < t.current_bet);
}

/// Primer asiento a la izquierda de `from` (sin incluirlo) con estado `Active`.
[[nodiscard]] constexpr u8 first_active_after(const Table& t, u8 from) noexcept {
	for (u8 k = 1u; k <= t.seat_count; ++k) {
		const u8 idx = ring_add(from, k, t.seat_count);
		if (t.seats[idx].status == SeatStatus::Active) {
			return idx;
		}
	}
	return kNoSeat;
}

/// Primer asiento a la izquierda de `from` que debe actuar; `kNoSeat` si la ronda acabó.
[[nodiscard]] constexpr u8 next_to_act(const Table& t, u8 from) noexcept {
	for (u8 k = 1u; k <= t.seat_count; ++k) {
		const u8 idx = ring_add(from, k, t.seat_count);
		if (needs_action(t, idx)) {
			return idx;
		}
	}
	return kNoSeat;
}

constexpr void deal_hole(Table& t, u8 count) noexcept {
	const u8 rounds = hole_cards_for(t.variant);
	for (u8 round = 0u; round < rounds; ++round) {
		for (u8 k = 0u; k < count; ++k) {
			const u8 seat = ring_add(t.button, static_cast<u8>(1u + k), count);
			if (t.seats[seat].status == SeatStatus::Active || t.seats[seat].status == SeatStatus::AllIn) {
				t.seats[seat].hole[round] = t.deck.deal();
			}
		}
	}
}

constexpr void deal_board(Table& t, u8 n) noexcept {
	for (u8 i = 0u; i < n; ++i) {
		t.board[t.board_count] = t.deck.deal();
		++t.board_count;
	}
}

} // namespace detail

/// Reparte una mano nueva. Los asientos sin fichas quedan `Out`. Baraja con `rng`
/// (misma semilla ⇒ misma mano) y reparte hole cards y ciegas. `variant` decide si
/// cada asiento recibe 2 (Hold'em) o 4 (Omaha) cartas; `structure`, la apuesta libre
/// (No-Limit) o fija con tope (Limit).
inline void start_hand(Table& t, eng::Xoroshiro64pp& rng, u8 seat_count, s32 starting_stack,
                       s32 small_blind, s32 big_blind, u8 button,
                       PokerVariant variant = PokerVariant::TexasHoldem,
                       BettingStructure structure = BettingStructure::NoLimit,
                       bool with_jokers = false) noexcept {
	t = Table {};
	t.seat_count = seat_count > kMaxSeats ? kMaxSeats : seat_count;
	t.small_blind = small_blind;
	t.big_blind = big_blind;
	t.min_raise = big_blind;
	t.variant = variant;
	t.structure = structure;
	t.with_jokers = with_jokers;
	t.button = detail::ring_add(0u, button, t.seat_count);
	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].status = starting_stack > 0 ? SeatStatus::Active : SeatStatus::Out;
		t.seats[i].stack = starting_stack;
		for (u8 c = 0u; c < kSeatCards; ++c) {
			t.seats[i].hole[c] = kNoCard;
		}
	}

	t.deck.reset(with_jokers);
	t.deck.shuffle(rng);

	const u8 count = t.seat_count;
	u8 sb;
	u8 bb;
	if (count == 2u) {
		sb = t.button;             // en heads-up el botón pone la ciega pequeña
		bb = detail::next_seat(t.button, count);
	} else {
		sb = detail::next_seat(t.button, count);
		bb = detail::next_seat(sb, count);
	}

	detail::deal_hole(t, count);

	detail::post(t, sb, small_blind);
	detail::post(t, bb, big_blind);
	t.current_bet = big_blind;

	if (count == 2u) {
		t.to_act = sb;
	} else {
		t.to_act = detail::next_seat(bb, count);
	}
	t.hand_over = false;
}

// ---------------------------------------------------------------------------
// Acciones legales
// ---------------------------------------------------------------------------

/// Acciones legales de `t.to_act`, escritas en `out` (máximo `max`). Devuelve cuántas.
[[nodiscard]] inline u8 legal_actions(const Table& t, Action* out, u8 max) noexcept {
	if (t.hand_over || t.to_act == kNoSeat) {
		return 0u;
	}
	const Seat& s = t.seats[t.to_act];
	const s32 owe = to_call(t, t.to_act);
	u8 n = 0u;

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

	if (t.structure == BettingStructure::Limit) {
		// Limit: subidas de tamaño fijo y tope de `kLimitMaxRaises` por calle. No se
		// ofrece all-in (la apuesta ya está acotada por el tope).
		if (!s.acted && t.raises_this_street < kLimitMaxRaises && s.stack > owe) {
			const s32 bet = limit_bet_size(t.street, t.small_blind, t.big_blind);
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

	// No-Limit: subida libre. Solo si la acción no está cerrada (`!acted`) y hay
	// fichas por encima del call.
	if (!s.acted && s.stack > owe) {
		s32 min_target = t.current_bet + t.min_raise;
		if (t.current_bet == 0) {
			min_target = t.big_blind;
		}
		const s32 max_target = s.street_bet + s.stack;
		if (max_target > min_target) {
			add(ActionType::Raise, min_target);
			// Subida de aproximación al bote (si aporta una opción distinta).
			const s32 pot_raise = t.current_bet + owe + t.pot + owe;
			s32 pot_target = pot_raise;
			if (pot_target != min_target && pot_target < max_target) {
				add(ActionType::Raise, pot_target);
			}
		}
	}

	if (s.stack > 0) {
		add(ActionType::AllIn, s.street_bet + s.stack);
	}
	return n;
}

// ---------------------------------------------------------------------------
// Botes laterales y showdown
// ---------------------------------------------------------------------------

namespace detail {

/// Reparte `pot` entre `winners[0..wn)` a partes iguales; el resto (fichas
/// indivisibles) va al primero por posición a la izquierda del botón.
constexpr void split_pot(Table& t, s32 pot, const u8* winners, u8 wn, s32* won) noexcept {
	if (wn == 0u || pot <= 0) {
		return;
	}
	u32 remainder_u = 0u;
	const s32 share = static_cast<s32>(divmod32(static_cast<u32>(pot), static_cast<u32>(wn), remainder_u));
	s32 remainder = static_cast<s32>(remainder_u);
	for (u8 i = 0u; i < wn; ++i) {
		won[winners[i]] += share;
	}
	while (remainder > 0) {
		bool handed = false;
		for (u8 k = 1u; k <= t.seat_count && remainder > 0; ++k) {
			const u8 idx = ring_add(t.button, k, t.seat_count);
			for (u8 i = 0u; i < wn; ++i) {
				if (winners[i] == idx) {
					won[idx] += 1;
					--remainder;
					handed = true;
					break;
				}
			}
		}
		if (!handed) {
			break;
		}
	}
}

} // namespace detail

/// Reparte la mano resuelta. Con un solo asiento no retirado se le da el bote sin
/// mostrar; con dos o más se evalúa el showdown y se construyen botes laterales por
/// niveles de aportación (`committed`).
inline void settle_showdown(Table& t) noexcept {
	s32 contrib[kMaxSeats] {};
	s32 won[kMaxSeats] {};
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
		u8 eligible[kMaxSeats] {};
		u8 en = 0u;
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (contrib[i] > 0) {
				pot += min_c;
				contrib[i] -= min_c;
				const SeatStatus st = t.seats[i].status;
				if (st == SeatStatus::Active || st == SeatStatus::AllIn) {
					eligible[en] = i;
					++en;
				}
			}
		}

		// Mejor mano entre los elegibles de este bote.
		HandValue best = kHandValueNone;
		u8 winners[kMaxSeats] {};
		u8 wn = 0u;
		for (u8 e = 0u; e < en; ++e) {
			const HandValue hv = seat_hand_value(t, eligible[e]);
			if (hv > best) {
				best = hv;
				wn = 0u;
				winners[wn] = eligible[e];
				++wn;
			} else if (hv == best && hv != kHandValueNone) {
				winners[wn] = eligible[e];
				++wn;
			}
		}
		detail::split_pot(t, pot, winners, wn, won);
	}

	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].stack += won[i];
	}
	t.pot = 0;
	t.hand_over = true;
	t.street = Street::Showdown;
	t.to_act = kNoSeat;
}

namespace detail {

constexpr void runout_board(Table& t) noexcept {
	while (t.board_count < kBoardCards) {
		deal_board(t, 1u);
	}
}

/// Cierra la ronda actual: resetea apuestas, reparte la siguiente calle y decide
/// quién actúa. Si todos están all-in, corre el resto del tablero y hace showdown.
inline void advance_street(Table& t) noexcept {
	for (u8 i = 0u; i < t.seat_count; ++i) {
		t.seats[i].street_bet = 0;
		t.seats[i].acted = false;
	}
	t.current_bet = 0;
	t.min_raise = t.big_blind;
	t.raises_this_street = 0u;

	switch (t.street) {
	case Street::Preflop:
		t.street = Street::Flop;
		deal_board(t, 3u);
		break;
	case Street::Flop:
		t.street = Street::Turn;
		deal_board(t, 1u);
		break;
	case Street::Turn:
		t.street = Street::River;
		deal_board(t, 1u);
		break;
	case Street::River:
		t.street = Street::Showdown;
		break;
	default:
		break;
	}

	if (t.street == Street::Showdown) {
		runout_board(t);
		settle_showdown(t);
		return;
	}
	if (seats_can_act(t) <= 1u) {
		runout_board(t);
		settle_showdown(t);
		return;
	}
	t.to_act = first_active_after(t, t.button);
}

constexpr void reset_acted_except(Table& t, u8 aggressor) noexcept {
	for (u8 i = 0u; i < t.seat_count; ++i) {
		if (i != aggressor && t.seats[i].status == SeatStatus::Active) {
			t.seats[i].acted = false;
		}
	}
}

} // namespace detail

/// Aplica la acción de `t.to_act` y avanza el estado (ronda, calle o fin de mano).
inline void apply_action(Table& t, const Action& action) noexcept {
	if (t.hand_over || t.to_act == kNoSeat) {
		return;
	}
	const u8 seat = t.to_act;
	Seat& s = t.seats[seat];
	s.acted = true;

	switch (action.type) {
	case ActionType::Fold:
		s.status = SeatStatus::Folded;
		break;
	case ActionType::Check:
		break;
	case ActionType::Call: {
		s32 owe = to_call(t, seat);
		if (owe > s.stack) {
			owe = s.stack;
		}
		detail::post(t, seat, owe);
		break;
	}
	case ActionType::Raise: {
		const s32 old_bet = t.current_bet;
		s32 target = action.amount;
		const s32 max_target = s.street_bet + s.stack;
		if (target > max_target) {
			target = max_target;
		}
		if (target < s.street_bet) {
			target = s.street_bet; // acción ilegal degradada a no-op, nunca resta fichas
		}
		const s32 add = target - s.street_bet;
		detail::post(t, seat, add);
		if (target > old_bet) {
			++t.raises_this_street;
		}
		if (s.status != SeatStatus::AllIn) {
			const s32 raise_size = target - old_bet;
			if (raise_size > t.min_raise) {
				t.min_raise = raise_size;
			}
			t.current_bet = target;
			detail::reset_acted_except(t, seat);
			t.last_aggressor = seat;
		} else if (target > t.current_bet) {
			// Subida corta all-in: sube la apuesta viva pero no reabre la acción.
			t.current_bet = target;
			t.last_aggressor = seat;
		}
		break;
	}
	case ActionType::AllIn: {
		const s32 old_bet = t.current_bet;
		const s32 target = s.street_bet + s.stack;
		detail::post(t, seat, s.stack);
		if (target > old_bet) {
			++t.raises_this_street;
			if (target - old_bet > t.min_raise) {
				t.min_raise = target - old_bet;
				detail::reset_acted_except(t, seat);
			}
			t.current_bet = target;
			t.last_aggressor = seat;
		}
		break;
	}
	}

	// Si solo queda un asiento en la mano, gana el bote sin showdown.
	if (seats_in_hand(t) <= 1u) {
		for (u8 i = 0u; i < t.seat_count; ++i) {
			if (t.seats[i].status == SeatStatus::Active || t.seats[i].status == SeatStatus::AllIn) {
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

	const u8 nxt = detail::next_to_act(t, seat);
	if (nxt == kNoSeat) {
		detail::advance_street(t);
	} else {
		t.to_act = nxt;
	}
}

/// Resuelve la mano si ya no hay acciones pendientes (por ejemplo, tras repartir
/// una calle en la que todos están all-in). Devuelve `true` si terminó.
[[nodiscard]] inline bool hand_finished(const Table& t) noexcept { return t.hand_over; }

} // namespace eng::cards
