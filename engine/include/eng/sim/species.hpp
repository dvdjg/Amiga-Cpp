#pragma once

/// \file species.hpp
/// **Especie** (`eng::sim`): la plantilla inmutable que comparten las criaturas de un
/// mismo tipo. Separar especie (tabla constante, vive en ROM) de individuo (estado
/// mutable) es lo que permite tener 200 criaturas en el A500: los datos que no cambian
/// no se copian por criatura.
///
/// La especie describe:
/// - **dieta** (`Diet`): qué come (decide si `Prey` es comida o competencia).
/// - **organización social** (`SocialStyle`): solitaria, manada, colmena, familia, territorial.
/// - **movimiento** (`MovementProfile`): bits de capacidades (andar, trepar, nadar, volar,
///   saltar) que el pathfinding consulta para filtrar tiles transitables.
/// - **capacidades** (`speed`, `vision`, `strength`, `size`) y **personalidad base**.
///
/// El movimiento es agnóstico de la proyección: los mismos bits sirven en 2D, isométrico
/// o 3D; cada `Projection` decide cómo traduce "puede trepar" a costes de tile.
///
/// Verificación: HOST-152.

#include <eng/core/types/types.hpp>
#include <eng/sim/personality.hpp>

namespace eng::sim {

/// Régimen alimenticio.
enum class Diet : eng::u8 {
	Herbivore = 0,
	Carnivore = 1,
	Omnivore = 2,
	Photosynth = 3, ///< no caza; se alimenta del entorno
	Detritus = 4,   ///< carroñero
};

/// Estructura social, que el módulo de sociedad/packs/colonia interpreta.
enum class SocialStyle : eng::u8 {
	Solitary = 0,
	Pack = 1,     ///< manada con liderazgo
	Hive = 2,     ///< colmena: la colonia manda sobre el individuo
	Family = 3,   ///< unidad familiar (crías)
	Territory = 4,///< defiende un área, no necesariamente gregario
	Swarm = 5,    ///< enjambre con castas y feromonas (hormigas, abejas, termitas)
};

/// Bits de capacidad de movimiento. El pathfinder filtra aristas/portales por ellos.
namespace movement {
inline constexpr eng::u8 walk = 0x01u;
inline constexpr eng::u8 climb = 0x02u;
inline constexpr eng::u8 swim = 0x04u;
inline constexpr eng::u8 fly = 0x08u;
inline constexpr eng::u8 jump = 0x10u;
inline constexpr eng::u8 dig = 0x20u;
inline constexpr eng::u8 ground = walk | climb | jump;
} // namespace movement

/// Plantilla de una especie. `id` es su índice en la tabla (coincide con `SpeciesId`).
struct Species {
	SpeciesId id = no_species;
	Diet diet = Diet::Omnivore;
	SocialStyle social = SocialStyle::Solitary;
	eng::u8 movement = movement::walk;
	eng::u8 size = 50u;    ///< tamaño relativo 0..100 (depredador vs presa)
	eng::u8 speed = 50u;   ///< velocidad relativa 0..100
	eng::u8 vision = 50u;  ///< alcance de detección visual 0..100
	eng::u8 hearing = 50u; ///< sensibilidad al ruido 0..100
	eng::u8 strength = 50u;///< poder de combate/agarre 0..100
	PersonalityTemplate personality {};

	[[nodiscard]] constexpr bool can_move(eng::u8 flag) const noexcept {
		return (movement & flag) != 0u;
	}

	[[nodiscard]] constexpr bool is_predator() const noexcept {
		return diet == Diet::Carnivore || diet == Diet::Omnivore;
	}

	[[nodiscard]] constexpr bool is_social() const noexcept {
		return social == SocialStyle::Pack || social == SocialStyle::Hive ||
		       social == SocialStyle::Family || social == SocialStyle::Swarm;
	}

	[[nodiscard]] constexpr bool is_swarm() const noexcept {
		return social == SocialStyle::Swarm;
	}
};

/// ¿La especie `a` puede depredar sobre la especie `b`? Regla mínima: carnívoros y
/// omnívoros cazan a lo que no es de su misma dieta y es más pequeño (o equivalente).
[[nodiscard]] constexpr bool preys_on(const Species& a, const Species& b) noexcept {
	if (!a.is_predator() || a.id == b.id) {
		return false;
	}
	return a.size >= b.size || a.diet == Diet::Carnivore;
}

} // namespace eng::sim
