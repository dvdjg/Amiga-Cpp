#pragma once

/// \file personality.hpp
/// **Personalidad** de una criatura (`eng::sim`): diez rasgos de `u8` en `[0, 100]` con
/// 50 como neutro. No dictan la acción, **modulan la utilidad** de los comportamientos
/// (mismo patrón que *Rain World*): una criatura nerviosa huye antes, una empática ayuda,
/// una autónoma se resiste a someterse, una diligente trabaja para la colonia.
///
/// Los rasgos se agrupan por dominio:
/// - **Supervivencia**: `bravery`, `nervousness`, `curiosity`.
/// - **Conflicto**: `aggression`, `dominance`, `autonomy` (deseo de no someterse).
/// - **Vínculo**: `sociability`, `empathy`, `loyalty`.
/// - **Trabajo**: `diligence` (forrajeo, construcción, cuidado; clave en enjambres).
///
/// La conversión a modificador porcentual la hace `trait_mod` de `types.hpp` y la aplica
/// `apply_mod`; todos los sesgos comparten una única fórmula entera (`muls.w`/`divs.w`).
///
/// Verificación: HOST-152.

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Diez rasgos con la misma escala. Cualquier combinación es válida.
struct Personality {
	eng::u8 aggression = 50u;  ///< inclinación a atacar/cazar
	eng::u8 bravery = 50u;     ///< resistencia al miedo (alto = huye menos)
	eng::u8 nervousness = 50u; ///< sensibilidad a la amenaza (alto = huye antes)
	eng::u8 sociability = 50u; ///< necesidad de grupo e interacción
	eng::u8 dominance = 50u;   ///< impulso de liderazgo y jerarquía
	eng::u8 curiosity = 50u;   ///< tendencia a explorar/deambular y aprender
	eng::u8 empathy = 50u;     ///< capacidad de compasión y ayuda
	eng::u8 loyalty = 50u;     ///< fidelidad al grupo/familia (mitiga desertar/atacar)
	eng::u8 autonomy = 50u;    ///< deseo de libertad (alto = se resiste a someterse)
	eng::u8 diligence = 50u;   ///< laboriosidad (forrajeo, construcción, cuidado)

	static constexpr eng::u8 neutral = 50u;
	static constexpr eng::u8 max = 100u;

	[[nodiscard]] constexpr eng::s16 aggression_mod() const noexcept {
		return trait_mod(aggression);
	}
	[[nodiscard]] constexpr eng::s16 bravery_mod() const noexcept { return trait_mod(bravery); }
	[[nodiscard]] constexpr eng::s16 nervousness_mod() const noexcept {
		return trait_mod(nervousness);
	}
	[[nodiscard]] constexpr eng::s16 sociability_mod() const noexcept {
		return trait_mod(sociability);
	}
	[[nodiscard]] constexpr eng::s16 dominance_mod() const noexcept {
		return trait_mod(dominance);
	}
	[[nodiscard]] constexpr eng::s16 curiosity_mod() const noexcept {
		return trait_mod(curiosity);
	}
	[[nodiscard]] constexpr eng::s16 empathy_mod() const noexcept { return trait_mod(empathy); }
	[[nodiscard]] constexpr eng::s16 loyalty_mod() const noexcept { return trait_mod(loyalty); }
	[[nodiscard]] constexpr eng::s16 autonomy_mod() const noexcept { return trait_mod(autonomy); }
	[[nodiscard]] constexpr eng::s16 diligence_mod() const noexcept {
		return trait_mod(diligence);
	}
};

/// Perfil de personalidad media de una especie, usado como base al nacer una criatura.
/// La variación individual (un lagarto más osado que otro) se logra con genética
/// (`genetics.hpp`) o sumando ruido determinista del RNG sobre estos valores.
struct PersonalityTemplate {
	eng::u8 aggression = 50u;
	eng::u8 bravery = 50u;
	eng::u8 nervousness = 50u;
	eng::u8 sociability = 50u;
	eng::u8 dominance = 50u;
	eng::u8 curiosity = 50u;
	eng::u8 empathy = 50u;
	eng::u8 loyalty = 50u;
	eng::u8 autonomy = 50u;
	eng::u8 diligence = 50u;

	/// Materializa el individuo. `jitter` (0 = sin ruido) desplaza cada rasgo con ruido
	/// determinista del PRNG, para que dos individuos de la misma especie difieran.
	template <class Rng>
	[[nodiscard]] constexpr Personality materialize(Rng& rng, eng::u8 jitter = 0u) const noexcept {
		Personality p {aggression, bravery, nervousness, sociability, dominance,
				curiosity,  empathy,   loyalty,     autonomy,    diligence};
		if (jitter != 0u) {
			p.aggression = jittered(p.aggression, rng, jitter);
			p.bravery = jittered(p.bravery, rng, jitter);
			p.nervousness = jittered(p.nervousness, rng, jitter);
			p.sociability = jittered(p.sociability, rng, jitter);
			p.dominance = jittered(p.dominance, rng, jitter);
			p.curiosity = jittered(p.curiosity, rng, jitter);
			p.empathy = jittered(p.empathy, rng, jitter);
			p.loyalty = jittered(p.loyalty, rng, jitter);
			p.autonomy = jittered(p.autonomy, rng, jitter);
			p.diligence = jittered(p.diligence, rng, jitter);
		}
		return p;
	}

private:
	template <class Rng>
	[[nodiscard]] static constexpr eng::u8 jittered(eng::u8 base, Rng& rng,
							eng::u8 amount) noexcept {
		const eng::s32 v = static_cast<eng::s32>(base) + eng::next_symmetric(rng, amount);
		if (v < 0) {
			return 0;
		}
		if (v > 100) {
			return 100;
		}
		return static_cast<eng::u8>(v);
	}
};

} // namespace eng::sim
