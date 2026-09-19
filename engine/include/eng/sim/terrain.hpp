#pragma once

/// \file terrain.hpp
/// **Representación algorítmica del mundo** (`eng::sim`): qué es cada zona para una
/// criatura que se mueve, al margen de que el juego se dibuje en 2D, isométrico o 3D.
///
/// No es un formato de tiles ni conoce sprites: es la **semántica** que consultan el
/// pathfinding y la decisión. El renderer mapea estos tipos a su geometría, y el
/// pathfinding real sigue siendo `eng::util::pathfinding` (que recibe `walkable`/`cost`
/// de aquí) o `eng::ai::navigation`.
///
/// - `TerrainKind`: suelo, terreno abrupto, muro, agua, pared escalable, hueco, borde,
///   zona peligrosa, cobertura.
/// - `TerrainProfile`: coste de entrada, `cover` y `shelter` (0..100) y `move_mask`, el
///   conjunto de capacidades (`movement::`) que permiten atravesarlo.
/// - `TerrainMap<W,H>`: rejilla de `TerrainKind` con `walkable`/`cost`/`cover` listos para
///   `eng::util::astar<W,H>`.
/// - `RegionTerrain`: clasificación macro de una región/habitación (terreno dominante,
///   abrigo y peligro), la que usa el mundo por regiones y el clima.
///
/// Verificación: HOST-158.

#include <eng/core/types.hpp>
#include <eng/sim/needs.hpp>
#include <eng/sim/species.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Qué es una celda para el movimiento.
enum class TerrainKind : eng::u8 {
	Floor = 0,  ///< suelo llano
	Rough = 1,  ///< terreno abrupto (más lento, con cobertura)
	Wall = 2,   ///< muro infranqueable
	Water = 3,  ///< agua (requiere nadar)
	Climb = 4,  ///< pared escalable
	Gap = 5,    ///< hueco/abismo (requiere saltar o volar)
	Ledge = 6,  ///< borde/repisa (escalar/saltar)
	Hazard = 7, ///< zona peligrosa (costosa pero transitable)
	Cover = 8,  ///< cobertura (protege de amenazas y clima)
	Count = 9,
};

/// Perfil de movimiento y protección de un terreno.
struct TerrainProfile {
	eng::u8 cost = 1;    ///< coste relativo de entrar (>= 1)
	eng::u8 cover = 0;   ///< cobertura frente a amenazas (0..100)
	eng::u8 shelter = 0; ///< mitigación de la exposición ambiental (0..100)
	eng::u8 move_mask = movement::walk; ///< capacidades que permiten atravesarlo (0 = muro)
};

/// Perfil de cada terreno. Un `move_mask == 0` significa infranqueable.
[[nodiscard]] constexpr TerrainProfile terrain_profile(TerrainKind k) noexcept {
	switch (k) {
		case TerrainKind::Floor: return {1u, 0u, 0u, movement::walk};
		case TerrainKind::Rough: return {3u, 30u, 10u, movement::walk};
		case TerrainKind::Wall: return {255u, 0u, 0u, 0u};
		case TerrainKind::Water: return {4u, 0u, 0u, static_cast<eng::u8>(movement::swim | movement::fly)};
		case TerrainKind::Climb: return {5u, 0u, 20u, static_cast<eng::u8>(movement::climb | movement::fly)};
		case TerrainKind::Gap: return {6u, 0u, 0u, static_cast<eng::u8>(movement::jump | movement::fly)};
		case TerrainKind::Ledge: return {4u, 10u, 20u, static_cast<eng::u8>(movement::climb | movement::jump | movement::fly)};
		case TerrainKind::Hazard: return {8u, 0u, 0u, movement::walk};
		case TerrainKind::Cover: return {2u, 70u, 60u, movement::walk};
		default: return {255u, 0u, 0u, 0u};
	}
}

/// ¿Una criatura con esas capacidades puede atravesar el terreno?
[[nodiscard]] constexpr bool can_traverse(eng::u8 movement_flags, TerrainKind k) noexcept {
	const TerrainProfile p = terrain_profile(k);
	return p.move_mask != 0u && (movement_flags & p.move_mask) != 0u;
}

[[nodiscard]] constexpr eng::u8 terrain_cost(TerrainKind k) noexcept {
	return terrain_profile(k).cost;
}
[[nodiscard]] constexpr eng::u8 terrain_cover(TerrainKind k) noexcept {
	return terrain_profile(k).cover;
}
[[nodiscard]] constexpr eng::u8 terrain_shelter(TerrainKind k) noexcept {
	return terrain_profile(k).shelter;
}

/// Nombre legible (diagnóstico; sin heap).
[[nodiscard]] constexpr const char* terrain_name(TerrainKind k) noexcept {
	switch (k) {
		case TerrainKind::Floor: return "floor";
		case TerrainKind::Rough: return "rough";
		case TerrainKind::Wall: return "wall";
		case TerrainKind::Water: return "water";
		case TerrainKind::Climb: return "climb";
		case TerrainKind::Gap: return "gap";
		case TerrainKind::Ledge: return "ledge";
		case TerrainKind::Hazard: return "hazard";
		case TerrainKind::Cover: return "cover";
		default: return "?";
	}
}

/// Clasificación macro de una región/habitación.
struct RegionTerrain {
	TerrainKind dominant = TerrainKind::Floor; ///< terreno predominante
	eng::u8 shelter = 0;                       ///< abrigo extra de la región (0..100)
	eng::u8 danger = 0;                        ///< peligro intrínseco (0..100)
};

/// Eventos que **cambian el terreno** en caliente (derrumbe, inundación, incendio, brote).
enum class TerrainEvent : eng::u8 {
	Collapse = 0, ///< derrumbe: deja escombros (terreno abrupto) y un peligro
	Flood = 1,    ///< inundación: convierte en agua y sube el clima de inundación
	Fire = 2,     ///< incendio: deja zona peligrosa y calor
	Regrowth = 3, ///< regeneración: recupera cobertura y limpia el clima
	Count = 4,
};

/// Parámetros de los eventos de terreno y su interacción con el clima.
struct TerrainEventParams {
	TerrainKind collapse_to = TerrainKind::Rough;
	TerrainKind flood_to = TerrainKind::Water;
	TerrainKind fire_to = TerrainKind::Hazard;
	TerrainKind regrowth_to = TerrainKind::Cover;
	eng::u8 collapse_hazard = 90u;
	eng::u8 flood_hazard = 100u;
	eng::u8 fire_hazard = 120u;
};

/// Terreno resultante de aplicar un evento.
[[nodiscard]] constexpr TerrainKind terrain_after_event(
	TerrainKind current, TerrainEvent e,
	const TerrainEventParams& p = TerrainEventParams {}) noexcept {
	(void)current;
	switch (e) {
		case TerrainEvent::Collapse: return p.collapse_to;
		case TerrainEvent::Flood: return p.flood_to;
		case TerrainEvent::Fire: return p.fire_to;
		case TerrainEvent::Regrowth: return p.regrowth_to;
		default: return current;
	}
}

/// Peligro climático que provoca un evento (`None` si no provoca ninguno).
[[nodiscard]] constexpr HazardKind hazard_from_event(TerrainEvent e) noexcept {
	switch (e) {
		case TerrainEvent::Flood: return HazardKind::Flood;
		case TerrainEvent::Fire: return HazardKind::Heat;
		case TerrainEvent::Collapse: return HazardKind::Dust;
		default: return HazardKind::None;
	}
}

/// Severidad del peligro que provoca un evento.
[[nodiscard]] constexpr eng::u8 hazard_severity_from_event(
	TerrainEvent e, const TerrainEventParams& p = TerrainEventParams {}) noexcept {
	switch (e) {
		case TerrainEvent::Flood: return p.flood_hazard;
		case TerrainEvent::Fire: return p.fire_hazard;
		case TerrainEvent::Collapse: return p.collapse_hazard;
		default: return 0u;
	}
}

/// Nombre legible (diagnóstico).
[[nodiscard]] constexpr const char* terrain_event_name(TerrainEvent e) noexcept {
	switch (e) {
		case TerrainEvent::Collapse: return "collapse";
		case TerrainEvent::Flood: return "flood";
		case TerrainEvent::Fire: return "fire";
		case TerrainEvent::Regrowth: return "regrowth";
		default: return "?";
	}
}

/// Rejilla de terreno lista para el pathfinding de `eng::util`.
template <eng::u16 W, eng::u16 H>
struct TerrainMap {
	static_assert(W > 0u && H > 0u, "TerrainMap: dimensiones > 0");
	static constexpr eng::usize cell_count = static_cast<eng::usize>(W) * H;

	eng::u8 cells[cell_count] {}; ///< `TerrainKind` por celda (Floor por defecto)

	[[nodiscard]] static constexpr eng::usize index(eng::u16 x, eng::u16 y) noexcept {
		return static_cast<eng::usize>(y) * W + x;
	}
	constexpr void set(eng::u16 x, eng::u16 y, TerrainKind k) noexcept {
		cells[index(x, y)] = static_cast<eng::u8>(k);
	}
	[[nodiscard]] constexpr TerrainKind kind(eng::u16 x, eng::u16 y) const noexcept {
		return static_cast<TerrainKind>(cells[index(x, y)]);
	}
	constexpr void fill(TerrainKind k) noexcept {
		for (eng::usize i = 0; i < cell_count; ++i) {
			cells[i] = static_cast<eng::u8>(k);
		}
	}

	/// Predicado para `eng::util::astar<W,H>`.
	[[nodiscard]] constexpr bool walkable(eng::usize idx, eng::u8 movement_flags) const noexcept {
		return idx < cell_count && can_traverse(movement_flags,
							static_cast<TerrainKind>(cells[idx]));
	}
	/// Coste de entrar en `idx` para `eng::util::astar<W,H>`.
	[[nodiscard]] constexpr eng::u16 cost(eng::usize idx) const noexcept {
		return idx < cell_count ? terrain_cost(static_cast<TerrainKind>(cells[idx]))
					: static_cast<eng::u16>(0xffffu);
	}
	[[nodiscard]] constexpr eng::u8 cover(eng::usize idx) const noexcept {
		return idx < cell_count ? terrain_cover(static_cast<TerrainKind>(cells[idx])) : 0u;
	}
};

} // namespace eng::sim
