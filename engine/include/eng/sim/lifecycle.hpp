#pragma once

/// \file lifecycle.hpp
/// **Ciclo de vida y reproducción** (`eng::sim`). Cierra el bucle evolutivo: una criatura
/// madura, se aparea y deja descendencia que hereda su genoma (`genetics.hpp`).
///
/// - `LifeStage`: infancia, juventud, adultez y vejez, derivadas de `age` (un contador
///   de desarrollo `u8` que el mundo incrementa despacio).
/// - `LifecycleParams`: umbrales de edad, requisitos para reproducirse (salud, hambre,
///   vínculo), coste parental, gestación y camada. Todo paramétrico.
/// - `stage_for`/`is_mature`/`can_reproduce`: consultas puras, sin estado.
/// - `newborn_genome`: combina dos genomas progenitores con `inherit` y aplica la
///   variación de la cría.
///
/// El **quién** se aparea y **dónde** nace lo decide `SimWorld`
/// (`try_reproduce`/`lay_brood`), usando relaciones (`Mate`/`Family`) y dificultad
/// ambiental. Una reina de enjambre usa `Colony::needed_caste` para decidir la casta de
/// la puesta.
///
/// Verificación: HOST-153 y HOST-154.

#include <eng/core/random.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/genetics.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Etapa de desarrollo.
enum class LifeStage : eng::u8 {
	Infant = 0,
	Juvenile = 1,
	Adult = 2,
	Elder = 3,
	Count = 4,
};

/// Parámetros del ciclo de vida.
struct LifecycleParams {
	eng::u8 juvenile_age = 50u; ///< edad a la que deja de ser cría
	eng::u8 adult_age = 110u;   ///< edad a la que es adulto (puede reproducirse)
	eng::u8 elder_age = 220u;   ///< edad a la que envejece
	eng::u8 death_age = 255u;   ///< edad de muerte natural
	eng::u8 maturation_period = 60u; ///< ticks de juego por unidad de edad
	eng::u8 repro_min_health = 60u;  ///< salud mínima para reproducirse
	eng::u8 repro_max_hunger = 120u; ///< hambre máxima (0 = saciado)
	eng::u8 repro_min_bond = 60u;    ///< vínculo/afecto mínimo con la pareja
	eng::u16 repro_cooldown = 240u;  ///< ticks mínimos entre puestas
	eng::u16 gestation_ticks = 90u;  ///< ticks de gestación antes del nacimiento
	eng::u8 newborn_health = 40u;    ///< salud con la que nace la cría
	eng::u8 litter = 1u;             ///< crías por puesta
	eng::u8 parental_cost = 60u;     ///< hambre/cansancio que cuesta criar
};

/// Estado reproductivo de una criatura (cooldown, gestación y pareja). Ocupa 6 bytes y
/// permite que el nacimiento ocurra **tras la gestación**, no de golpe.
struct ReproState {
	eng::u16 cooldown = 0;              ///< ticks que faltan para poder reproducirse
	eng::u16 gestation = 0;             ///< ticks que faltan para el parto (0 = ninguna)
	EntityId partner = no_entity;       ///< pareja de la gestación en curso
	eng::u8 flags = 0;                  ///< reservado (p. ej. cuidando)

	[[nodiscard]] constexpr bool ready() const noexcept {
		return cooldown == 0u && gestation == 0u;
	}
	[[nodiscard]] constexpr bool gestating() const noexcept { return gestation > 0u; }
};

/// Etapa según la edad.
[[nodiscard]] constexpr LifeStage stage_for(eng::u8 age,
					    const LifecycleParams& p = LifecycleParams {}) noexcept {
	if (age < p.juvenile_age) {
		return LifeStage::Infant;
	}
	if (age < p.adult_age) {
		return LifeStage::Juvenile;
	}
	if (age < p.elder_age) {
		return LifeStage::Adult;
	}
	return LifeStage::Elder;
}

/// ¿Está en edad reproductiva (adulto, aún no anciano)?
[[nodiscard]] constexpr bool is_mature(eng::u8 age,
				       const LifecycleParams& p = LifecycleParams {}) noexcept {
	return stage_for(age, p) == LifeStage::Adult;
}

/// ¿Puede reproducirse ahora? (madurez + salud + no hambriento ni agotado)
[[nodiscard]] constexpr bool can_reproduce(eng::u8 age, eng::u8 health, eng::u8 hunger,
					   eng::u8 fatigue,
					   const LifecycleParams& p = LifecycleParams {}) noexcept {
	return is_mature(age, p) && health >= p.repro_min_health &&
	       hunger <= p.repro_max_hunger && fatigue <= p.repro_max_hunger;
}

/// Avanza la edad de forma saturada.
constexpr void grow(eng::u8& age) noexcept {
	if (age < 255u) {
		++age;
	}
}

/// ¿Ha llegado al final de su vida natural?
[[nodiscard]] constexpr bool died_of_old_age(eng::u8 age,
					     const LifecycleParams& p = LifecycleParams {}) noexcept {
	return age >= p.death_age;
}

/// Genoma de una cría a partir de dos progenitores. `jitter` añade variación genética.
template <class Rng>
[[nodiscard]] constexpr Genome newborn_genome(const Genome& a, const Genome& b, Rng& rng,
					      const GeneticsParams& gp = GeneticsParams {}) noexcept {
	return inherit(a, b, rng, gp);
}

} // namespace eng::sim
