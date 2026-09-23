#pragma once

/// \file persona.hpp
/// **Persona** de un personaje controlado por la máquina (`eng::sim`, capa de persona):
/// la unión de sus rasgos base (`Personality`), rasgos de psique (`PsycheTraits`),
/// aptitudes (`Skills`) y defectos (`Flaws`), más el arquetipo del que procede.
///
/// `Persona` es el **carácter estable**: no cambia durante la partida. Lo temporal (tensión,
/// tilt, racha) vive en `PsycheState` (`psyche.hpp`) y lo instantáneo (gestos) en
/// `expression.hpp`. La persona se **materializa** desde un arquetipo (`archetypes.hpp`)
/// con jitter determinista, de modo que dos individuos del mismo arquetipo difieren sin
/// dejar de ser reconocibles.
///
/// Todo entero, sin heap; usa `trait_mod`/`apply_mod` para los modificadores.
///
/// Verificación: HOST-199.

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/sim/archetypes.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/psyche_traits.hpp>

namespace eng::sim {

/// Carácter estable de un personaje.
struct Persona {
	Personality base {};
	PsycheTraits psyche {};
	Skills skills {};
	Flaws flaws {};
	Archetype archetype = Archetype::Flematico;

	/// Nombre del arquetipo (diagnóstico).
	[[nodiscard]] constexpr const char* name() const noexcept { return archetype_name(archetype); }
};

namespace detail {

/// Desplaza un rasgo `u8` `[0,100]` con ruido simétrico determinista.
template <class Rng>
[[nodiscard]] constexpr eng::u8 persona_jitter(eng::u8 value, Rng& rng, eng::u8 amount) noexcept {
	const eng::s32 v = static_cast<eng::s32>(value) + eng::next_symmetric(rng, amount);
	if (v < 0) {
		return 0u;
	}
	if (v > 100) {
		return 100u;
	}
	return static_cast<eng::u8>(v);
}

} // namespace detail

/// Materializa una persona desde una definición de arquetipo. `jitter` (0 = idéntico al
/// arquetipo) desplaza cada rasgo con ruido determinista del PRNG; las aptitudes y los
/// defectos no se alteran (son pericia y taras, no carácter).
template <class Rng>
[[nodiscard]] constexpr Persona materialize(const ArchetypeDef& def, Rng& rng,
                                            eng::u8 jitter = 0u) noexcept {
	Persona p {};
	p.archetype = def.id;
	p.base = def.base;
	p.psyche = def.psyche;
	p.skills = def.skills;
	p.flaws = def.flaws;
	if (jitter != 0u) {
		p.base.aggression = detail::persona_jitter(p.base.aggression, rng, jitter);
		p.base.bravery = detail::persona_jitter(p.base.bravery, rng, jitter);
		p.base.nervousness = detail::persona_jitter(p.base.nervousness, rng, jitter);
		p.base.sociability = detail::persona_jitter(p.base.sociability, rng, jitter);
		p.base.dominance = detail::persona_jitter(p.base.dominance, rng, jitter);
		p.base.curiosity = detail::persona_jitter(p.base.curiosity, rng, jitter);
		p.base.empathy = detail::persona_jitter(p.base.empathy, rng, jitter);
		p.base.loyalty = detail::persona_jitter(p.base.loyalty, rng, jitter);
		p.base.autonomy = detail::persona_jitter(p.base.autonomy, rng, jitter);
		p.base.diligence = detail::persona_jitter(p.base.diligence, rng, jitter);
		p.psyche.composure = detail::persona_jitter(p.psyche.composure, rng, jitter);
		p.psyche.concentration = detail::persona_jitter(p.psyche.concentration, rng, jitter);
		p.psyche.patience = detail::persona_jitter(p.psyche.patience, rng, jitter);
		p.psyche.temper = detail::persona_jitter(p.psyche.temper, rng, jitter);
		p.psyche.impulsivity = detail::persona_jitter(p.psyche.impulsivity, rng, jitter);
		p.psyche.deceit = detail::persona_jitter(p.psyche.deceit, rng, jitter);
		p.psyche.gullibility = detail::persona_jitter(p.psyche.gullibility, rng, jitter);
		p.psyche.suspicion = detail::persona_jitter(p.psyche.suspicion, rng, jitter);
		p.psyche.honesty = detail::persona_jitter(p.psyche.honesty, rng, jitter);
		p.psyche.generosity = detail::persona_jitter(p.psyche.generosity, rng, jitter);
		p.psyche.vanity = detail::persona_jitter(p.psyche.vanity, rng, jitter);
		p.psyche.morality = detail::persona_jitter(p.psyche.morality, rng, jitter);
		p.psyche.humor = detail::persona_jitter(p.psyche.humor, rng, jitter);
		p.psyche.talkativeness = detail::persona_jitter(p.psyche.talkativeness, rng, jitter);
		p.psyche.self_esteem = detail::persona_jitter(p.psyche.self_esteem, rng, jitter);
		p.psyche.attention = detail::persona_jitter(p.psyche.attention, rng, jitter);
		p.psyche.social_memory = detail::persona_jitter(p.psyche.social_memory, rng, jitter);
		p.psyche.superstition = detail::persona_jitter(p.psyche.superstition, rng, jitter);
		p.psyche.risk = detail::persona_jitter(p.psyche.risk, rng, jitter);
		p.psyche.optimism = detail::persona_jitter(p.psyche.optimism, rng, jitter);
	}
	return p;
}

/// Materializa una persona por arquetipo.
template <class Rng>
[[nodiscard]] constexpr Persona make_persona(Archetype a, Rng& rng, eng::u8 jitter = 0u) noexcept {
	return materialize(archetype_def(a), rng, jitter);
}

} // namespace eng::sim
