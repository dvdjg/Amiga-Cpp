#pragma once

/// \file mental_map.hpp
/// **Mapa mental aplicado al movimiento** (`eng::sim`): traduce la memoria espacial
/// (`KnowledgeSet` con lugares recordados) en señales que usan el pathfinding fino y el
/// campo de influencia. Así el recuerdo no se queda en datos: **guía por dónde se mueve**.
///
/// - `place_bias`: sesgo de una región (negativo = atractiva, positivo = peligrosa) según
///   lo recordado (refugio/comida bajan el coste; peligro lo sube).
/// - `MentalOverlay<W,H>`: capa de coste adicional por celda para
///   `eng::util::astar` (se combina con el coste del `TerrainMap`).
/// - `deposit_mental_danger`: marca el peligro recordado en un
///   `eng::ai::InfluenceMap` para que el steering o el flow field lo eviten.
///
/// El mapeo celda→región lo aporta el juego (`room_at(idx)`), porque la geometría concreta
/// (2D/iso/3D) es cosa suya; el engine solo conoce la semántica.
///
/// Verificación: HOST-164.

#include <eng/ai/perception/influence_map.hpp>
#include <eng/core/types/types.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/memory.hpp>
#include <eng/sim/terrain.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Parámetros del mapa mental.
struct MentalMapParams {
	eng::u8 min_confidence = 100u;  ///< confianza mínima para que un lugar influya
	eng::u8 shelter_discount = 40u; ///< cuánto abarata un refugio recordado
	eng::u8 food_discount = 25u;    ///< cuánto abarata una fuente de comida recordada
	eng::u8 danger_cost = 80u;      ///< cuánto encarece un peligro recordado
	eng::u8 influence_danger = 150; ///< influencia que deposita un peligro recordado
};

/// Sesgo de una región `[-128, 127]`: negativo atrae, positivo disuade.
[[nodiscard]] constexpr eng::s16 place_bias(const KnowledgeSet& knowledge, RoomId room,
					    const MentalMapParams& p = MentalMapParams {}) noexcept {
	if (room == no_room) {
		return 0;
	}
	eng::s16 bias = 0;
	const eng::u8 shelter = confidence_for(knowledge, KnowledgeKind::Shelter, room);
	if (shelter >= p.min_confidence) {
		bias = static_cast<eng::s16>(bias - u8_scale(shelter, p.shelter_discount));
	}
	const eng::u8 food = confidence_for(knowledge, KnowledgeKind::FoodSource, room);
	if (food >= p.min_confidence) {
		bias = static_cast<eng::s16>(bias - u8_scale(food, p.food_discount));
	}
	const eng::u8 danger = confidence_for(knowledge, KnowledgeKind::Danger, room);
	if (danger >= p.min_confidence) {
		bias = static_cast<eng::s16>(bias + u8_scale(danger, p.danger_cost));
	}
	if (bias > 127) {
		bias = 127;
	}
	if (bias < -128) {
		bias = -128;
	}
	return bias;
}

/// Capa de coste adicional por celda, construida desde el mapa mental.
template <eng::u16 W, eng::u16 H>
struct MentalOverlay {
	static constexpr eng::usize size = static_cast<eng::usize>(W) * H;
	eng::s8 delta[size] {};

	/// Rellena la capa usando `room_at(idx) -> RoomId`.
	template <class RoomAt>
	constexpr void stamp(const KnowledgeSet& knowledge, RoomAt room_at,
			     const MentalMapParams& p = MentalMapParams {}) noexcept {
		for (eng::usize i = 0; i < size; ++i) {
			const eng::s16 b = place_bias(knowledge, room_at(i), p);
			delta[i] = static_cast<eng::s8>(b);
		}
	}

	/// Coste para `eng::util::astar`: el del terreno más el sesgo del mapa mental (mín. 1).
	[[nodiscard]] constexpr eng::u16 cost(eng::usize idx, eng::u16 base) const noexcept {
		if (idx >= size) {
			return base;
		}
		eng::s16 c = static_cast<eng::s16>(base) + delta[idx];
		if (c < 1) {
			c = 1;
		}
		return static_cast<eng::u16>(c);
	}
};

/// Deposita el **peligro recordado** en un mapa de influencia (para que el steering o el
/// flow field lo eviten). Rellena usando `room_at(idx) -> RoomId`.
template <class Map, class RoomAt>
constexpr void deposit_mental_danger(const KnowledgeSet& knowledge, Map& influence,
				     RoomAt room_at,
				     const MentalMapParams& p = MentalMapParams {}) noexcept {
	for (eng::usize i = 0; i < Map::size; ++i) {
		const RoomId r = room_at(i);
		const eng::u8 danger = confidence_for(knowledge, KnowledgeKind::Danger, r);
		if (danger >= p.min_confidence) {
			influence.deposit(static_cast<eng::u16>(i),
					  u8_scale(danger, p.influence_danger));
		}
	}
}

} // namespace eng::sim
