#pragma once

/// \file creature.hpp
/// `eng::sim::AbstractCreature`: el **estado completo de una criatura** en el plano
/// abstracto. Es un agregado de datos compacto (necesidades, personalidad, mente,
/// trackers y relaciones) más la identidad y la localización; no tiene lógica de render
/// ni punteros, así que cabe en un array contiguo y viaja igual en 2D, isométrico o 3D.
///
/// Capacidades como parámetro de plantilla: `MaxTrackers` y `MaxRelations` fijan el
/// tamaño exacto (los arrays viven inline), de modo que el presupuesto de RAM se decide
/// en compilación. Un perfil de A500 usa valores pequeños; una máquina con más memoria
/// instancia `AbstractCreature<10, 10>` sin cambiar el algoritmo.
///
/// Qué NO hay aquí: el camino (`Path`) y el cuerpo físico/procedural de la criatura
/// **realizada**; viven en la capa de representación y solo se materializan cerca de la
/// cámara (ver `docs/engine/architecture/SIM_ECOSYSTEM.md`).
///
/// Verificación: HOST-152.

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/behavior.hpp>
#include <eng/sim/genetics.hpp>
#include <eng/sim/inventory.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/lifecycle.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/needs.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/relationship.hpp>
#include <eng/sim/senses.hpp>
#include <eng/sim/tracker.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Bits de estado de una criatura.
namespace flags {
inline constexpr eng::u8 alive = 0x01u;    ///< viva (0 = cadáver)
inline constexpr eng::u8 realized = 0x02u; ///< simulada con detalle cerca de cámara
inline constexpr eng::u8 in_den = 0x04u;   ///< dentro de su refugio
inline constexpr eng::u8 asleep = 0x08u;   ///< durmiendo
inline constexpr eng::u8 carrying = 0x10u; ///< transporta un objeto
inline constexpr eng::u8 injured = 0x20u;  ///< herida abierta (sangra/cojea)
inline constexpr eng::u8 dormant = 0x40u;  ///< fuera del LOD: no se simula (lejos del jugador)
} // namespace flags

/// Estado lógico de una criatura. Los valores por defecto describen una criatura viva,
/// criada en el sistema 0 (room 0), con personalidad y necesidades neutras.
template <eng::u8 MaxTrackers = kDefaultMaxTrackers, eng::u8 MaxRelations = kDefaultMaxRelations>
struct AbstractCreature {
	// --- Identidad y localización ---
	EntityId id = no_entity;
	SpeciesId species = no_species;
	FactionId faction = 0;
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	RoomId den_room = no_room;
	eng::s16 den_x = 0;
	eng::s16 den_y = 0;

	// --- Estado vital ---
	eng::u8 health = 100u; ///< 0 = muerto
	eng::u8 flags = flags::alive;
	eng::u8 age = 0;       ///< etapa/edad simplificada (0 = cría, 255 = viejo)

	// --- Componentes ---
	Needs needs {};
	Personality personality {};
	Senses senses {};               ///< capacidades sensoriales (visión, oído, olfato...)
	Mind mind {};
	Genome genome {};               ///< dotación hereditaria (enjambres/crías)
	KnowledgeSet knowledge {};      ///< creencias aprendidas y compartibles
	Inventory carrying {};          ///< objetos que transporta
	ReproState repro {};            ///< cooldown/gestación/pareja
	Behavior behavior = Behavior::Idle;
	Score behavior_score = 0;
	eng::u8 lod_blend = 255u; ///< 255 = plenamente realizada; <255 = despertando (LOD)

	// --- Percepción y vínculos (capacidad fija) ---
	eng::util::StaticVector<Tracker, MaxTrackers> trackers {};
	eng::util::StaticVector<Relationship, MaxRelations> relationships {};

	// --- Consultas ---
	[[nodiscard]] constexpr bool alive() const noexcept { return (flags & flags::alive) != 0u; }
	[[nodiscard]] constexpr bool realized() const noexcept {
		return (flags & flags::realized) != 0u;
	}
	[[nodiscard]] constexpr bool dormant() const noexcept {
		return (flags & flags::dormant) != 0u;
	}
	/// Realizada y ya **despierta del todo** (transición de LOD completada).
	[[nodiscard]] constexpr bool lod_ready() const noexcept {
		return realized() && lod_blend == 255u;
	}
	constexpr void set_lod_blend(eng::u8 v) noexcept { lod_blend = v; }
	constexpr void set_dormant(bool on) noexcept {
		if (on) {
			flags = static_cast<eng::u8>(flags | flags::dormant);
		} else {
			flags = static_cast<eng::u8>(flags & static_cast<eng::u8>(~flags::dormant));
		}
	}
	[[nodiscard]] constexpr bool at_home() const noexcept {
		return den_room != no_room && room == den_room;
	}

	// --- Mutaciones ---
	constexpr void set_realized(bool on) noexcept {
		if (on) {
			flags = static_cast<eng::u8>(flags | flags::realized);
		} else {
			flags = static_cast<eng::u8>(flags & static_cast<eng::u8>(~flags::realized));
		}
	}

	/// Fija el refugio (guarida) al que volver con la lluvia o al estar herida.
	constexpr void set_den(RoomId r, eng::s16 dx, eng::s16 dy) noexcept {
		den_room = r;
		den_x = dx;
		den_y = dy;
	}

	/// Aplica daño; marca `injured` y mata si la salud llega a 0.
	constexpr void take_damage(eng::u8 amount) noexcept {
		health = u8_sat_sub(health, amount);
		hurt(needs, amount);
		flags = static_cast<eng::u8>(flags | flags::injured);
		if (health == 0u) {
			flags = static_cast<eng::u8>(flags & static_cast<eng::u8>(~flags::alive));
			set_realized(false);
		}
	}

	/// Cura daño y limpia el flag de herida si la salud se recupera.
	constexpr void restore(eng::u8 amount) noexcept {
		health = u8_sat_add(health, amount);
		if (health > 80u) {
			flags = static_cast<eng::u8>(flags & static_cast<eng::u8>(~flags::injured));
		}
	}
};

} // namespace eng::sim
