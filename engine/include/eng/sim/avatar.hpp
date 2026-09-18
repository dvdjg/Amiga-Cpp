#pragma once

/// \file avatar.hpp
/// **Jugador simulado** (`eng::sim`): un avatar con IA que recorre el mundo con los mismos
/// sentidos, necesidades y decisiones que las criaturas. Sirve para (a) que un bot juegue y
/// el mundo reaccione a su alrededor, y (b) validar en host la experiencia de juego
/// (cuánto percibe, qué ve, cómo cambia el mundo) sin depender del render.
///
/// - `PlayerIntent`: lo que el avatar quiere hacer este tick (deambular, comer, refugiarse,
///   descansar, socializar), derivado de sus necesidades y del entorno.
/// - `player_step`: mueve y actúa al avatar según su intención.
///
/// La riqueza percibida y la carga se gobiernan con el **LOD** (`lod.hpp`): el avatar es el
/// observador alrededor del cual se realiza el mundo.
///
/// Verificación: HOST-174.

#include <eng/core/types.hpp>
#include <eng/sim/biome.hpp>
#include <eng/sim/lod.hpp>
#include <eng/sim/needs.hpp>
#include <eng/sim/types.hpp>
#include <eng/sim/world.hpp>

namespace eng::sim {

/// Intención del jugador simulado.
enum class PlayerIntent : eng::u8 {
	Wander = 0,
	Feed = 1,
	Shelter = 2,
	Rest = 3,
	Social = 4,
	Count = 5,
};

/// Parámetros del jugador simulado.
struct PlayerParams {
	eng::u8 hunger_act = 120u;   ///< hambre a la que busca comida
	eng::u8 fatigue_act = 160u;  ///< cansancio al que descansa
	eng::u8 shelter_act = 100u;  ///< exposición a la que busca refugio
	eng::u8 social_act = 140u;   ///< necesidad social a la que busca compañía
	eng::u8 move_chance = 200u;  ///< probabilidad de moverse por tick (0..255)
	eng::u8 feed_amount = 80u;   ///< cuánto sacia una comida
};

/// Nombre legible.
[[nodiscard]] constexpr const char* player_intent_name(PlayerIntent i) noexcept {
	switch (i) {
		case PlayerIntent::Wander: return "wander";
		case PlayerIntent::Feed: return "feed";
		case PlayerIntent::Shelter: return "shelter";
		case PlayerIntent::Rest: return "rest";
		case PlayerIntent::Social: return "social";
		default: return "?";
	}
}

/// Decide la intención según las necesidades (la más urgente manda).
[[nodiscard]] constexpr PlayerIntent intent_for(const Needs& n,
						const PlayerParams& p = PlayerParams {}) noexcept {
	if (n.exposure >= p.shelter_act && n.exposure > n.hunger) {
		return PlayerIntent::Shelter;
	}
	if (n.hunger >= p.hunger_act) {
		return PlayerIntent::Feed;
	}
	if (n.fatigue >= p.fatigue_act) {
		return PlayerIntent::Rest;
	}
	if (n.social >= p.social_act) {
		return PlayerIntent::Social;
	}
	return PlayerIntent::Wander;
}

namespace detail {
constexpr void step_axis(eng::s16& v, eng::s16 target, eng::s16 limit) noexcept {
	if (v < target) {
		v = static_cast<eng::s16>(v + 1);
	} else if (v > target) {
		v = static_cast<eng::s16>(v - 1);
	}
	if (v < 0) {
		v = 0;
	}
	if (v > limit) {
		v = limit;
	}
}
} // namespace detail

/// Avanza el avatar un tick: decide intención, se mueve hacia su objetivo y actúa (come).
/// Devuelve la intención elegida. `W` es cualquier `SimWorld<…>`.
template <class W, class Rng>
[[nodiscard]] constexpr PlayerIntent player_step(W& w, EntityId id, Rng& rng,
						 const PlayerParams& p = PlayerParams {}) noexcept {
	auto* c = w.find(id);
	if (c == nullptr || !c->alive()) {
		return PlayerIntent::Wander;
	}
	const PlayerIntent intent = intent_for(c->needs, p);

	// Objetivo de movimiento según intención.
	eng::s16 tx = c->x;
	eng::s16 ty = c->y;
	switch (intent) {
		case PlayerIntent::Shelter: {
			const RoomId ref = w.preferred_refuge(*c);
			if (ref != no_room && ref != c->room) {
				const RoomId step = w.route_first_step(c->room, ref);
				if (step != no_room) {
					c->room = step;
				}
			}
			break;
		}
		case PlayerIntent::Feed: {
			// Busca la región vecina/actual con más comida.
			RoomId best = c->room;
			eng::u8 best_food = biome_food(w.biome(c->room));
			for (eng::u8 k = 0; k < w.room_degree(c->room); ++k) {
				const RoomId nb = w.room_link(c->room, k);
				const eng::u8 f = biome_food(w.biome(nb));
				if (f > best_food) {
					best_food = f;
					best = nb;
				}
			}
			if (best != c->room) {
				c->room = best;
			}
			break;
		}
		case PlayerIntent::Social: {
			if (const Tracker* fr = best_attention_tracker(c->trackers, TrackerKind::Friend);
			    fr != nullptr) {
				tx = fr->x;
				ty = fr->y;
			}
			break;
		}
		default:
			break;
	}

	// Movimiento (hacia el objetivo si lo hay, si no al azar).
	if (eng::chance(rng, p.move_chance, 255u)) {
		if (tx != c->x || ty != c->y) {
			detail::step_axis(c->x, tx, static_cast<eng::s16>(40));
			detail::step_axis(c->y, ty, static_cast<eng::s16>(40));
		} else {
			c->x = static_cast<eng::s16>(c->x + static_cast<eng::s16>(rng.next_mod(3u)) - 1);
			c->y = static_cast<eng::s16>(c->y + static_cast<eng::s16>(rng.next_mod(3u)) - 1);
			if (c->x < 0) {
				c->x = 0;
			}
			if (c->y < 0) {
				c->y = 0;
			}
		}
	}

	// Actuar: comer si está en región con comida.
	if (intent == PlayerIntent::Feed && biome_food(w.biome(c->room)) >= 40u) {
		feed(c->needs, p.feed_amount);
		c->mind.remember(MemoryKind::Ate, no_entity, 80u);
	}
	return intent;
}

/// Entrada **humana** del avatar (mando/direccional). Un humano sustituye a la IA sin tocar
/// el mundo: basta con llamar a `player_control` en vez de a `player_step`.
struct PlayerInput {
	eng::s16 dx = 0;       ///< avance en X (celdas por frame)
	eng::s16 dy = 0;       ///< avance en Y
	bool interact = false; ///< comer/usar en la región actual
	bool rest = false;     ///< descansar
};

/// Aplica la entrada humana al avatar: mueve y actúa. Devuelve `true` si actuó.
template <class W>
constexpr bool player_control(W& w, EntityId id, const PlayerInput& in,
			      const PlayerParams& p = PlayerParams {}) noexcept {
	auto* c = w.find(id);
	if (c == nullptr || !c->alive()) {
		return false;
	}
	eng::s16 nx = static_cast<eng::s16>(c->x + in.dx);
	eng::s16 ny = static_cast<eng::s16>(c->y + in.dy);
	if (nx < 0) {
		nx = 0;
	} else if (nx > 40) {
		nx = 40;
	}
	if (ny < 0) {
		ny = 0;
	} else if (ny > 40) {
		ny = 40;
	}
	c->x = nx;
	c->y = ny;

	bool acted = false;
	if (in.interact && biome_food(w.biome(c->room)) >= 40u) {
		feed(c->needs, p.feed_amount);
		c->mind.remember(MemoryKind::Ate, no_entity, 80u);
		acted = true;
	}
	if (in.rest) {
		rest(c->needs, 60u);
		acted = true;
	}
	return acted;
}

/// Resumen de lo que percibe el jugador y de la carga de simulación (para depurar LOD).
struct PlayerView {	eng::u8 observed = 0;  ///< observaciones producidas por sus sentidos
	eng::u8 realized = 0;  ///< criaturas a detalle cerca
	eng::u8 abstract = 0;  ///< criaturas en tick abstracto
	eng::u8 dormant = 0;   ///< criaturas congeladas lejos
};

/// Calcula el resumen del jugador a partir del mundo (usa los contadores de LOD).
template <class W>
[[nodiscard]] constexpr PlayerView player_view(const W& w, EntityId,
					       eng::u8 observed) noexcept {
	PlayerView v {};
	v.observed = observed;
	v.realized = w.realized_count();
	v.abstract = w.abstract_count();
	v.dormant = w.dormant_count();
	return v;
}

} // namespace eng::sim
