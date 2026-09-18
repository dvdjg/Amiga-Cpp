#pragma once

/// \file hierarchy.hpp
/// **Jerarquía, sumisión y libertad** (`eng::sim`). Las criaturas entienden rangos: quién
/// es más fuerte, cuándo conviene **someterse** y cuándo merece la pena **resistir**.
/// No hay un "estado de sumisión" global; hay una **evaluación paramétrica** de poder y
/// una decisión (comportamientos `Submit`/`Defy`) que sale de ella.
///
/// Modelo:
/// - `contest_power`: poder de combate/estatus de una criatura a partir de su dominancia,
///   fuerza y estado físico. Es una media ponderada entera (`HierarchyParams`).
/// - `should_submit`: compara dos poderes y decide si conviene ceder. Un individuo
///   autónomo exige más margen para someterse; uno deferente cede antes; el miedo ayuda a
///   someterse.
/// - `submission_score` y `defiance_score`: presiones `[0,1000]` para los comportamientos
///   de ceder/resistir, listas para `apply_mod`.
///
/// `rank_band` convierte un poder en una banda (0..4) útil para reglas sociales, animación
/// de postura o UI, sin acoplar el modelo a una representación.
///
/// Todo es determinista y entero; las reglas se reajustan cambiando `HierarchyParams`.
///
/// Verificación: HOST-154.

#include <eng/core/types.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Pesos y márgenes del modelo jerárquico (`Rng` no interviene: es determinista).
struct HierarchyParams {
	eng::u8 w_dominance = 60u; ///< peso de la dominancia en el poder
	eng::u8 w_strength = 70u;  ///< peso de la fuerza
	eng::u8 w_condition = 40u; ///< peso del estado físico (salud)
	eng::u8 submit_margin = 25u;  ///< margen de poder para ceder "gratis"
	eng::u8 autonomy_resist = 60u;///< cuánto la autonomía encarece someterse
	eng::u8 deference_accept = 40u;///< cuánto la deferencia abarata someterse
	eng::u8 prudence = 80u;    ///< cuánto frena la inferioridad la rebeldía
	eng::u8 anger_defiance = 35u;///< cuánto la ira empuja a resistir
	eng::u8 fear_submission = 45u;///< cuánto el miedo empuja a someterse
};

/// Poder de una criatura. `dominance`/`strength` en `[0,100]`, `condition` en `[0,100]`.
[[nodiscard]] constexpr eng::u8 contest_power(eng::u8 dominance, eng::u8 strength,
					      eng::u8 condition,
					      const HierarchyParams& p = HierarchyParams {}) noexcept {
	const eng::u16 wsum = static_cast<eng::u16>(p.w_dominance) + p.w_strength + p.w_condition;
	if (wsum == 0u) {
		return 0u;
	}
	const eng::u16 acc = static_cast<eng::u16>(static_cast<eng::u16>(dominance) * p.w_dominance +
						   static_cast<eng::u16>(strength) * p.w_strength +
						   static_cast<eng::u16>(condition) * p.w_condition);
	return static_cast<eng::u8>(div_u16(acc, wsum));
}

/// ¿Conviene someterse a alguien de poder `other_power`? El margen efectivo sube con la
/// autonomía y baja con la deferencia y el miedo.
[[nodiscard]] constexpr bool should_submit(eng::u8 self_power, eng::u8 other_power,
					   const Personality& personality, const Mind& mind,
					   const HierarchyParams& p = HierarchyParams {}) noexcept {
	const eng::s16 aut = static_cast<eng::s16>(personality.autonomy) - 50;
	const eng::s16 def = static_cast<eng::s16>(mind.deference) - 128;
	eng::s16 margin = p.submit_margin;
	margin = static_cast<eng::s16>(
		margin + eng::math::div_wide(static_cast<eng::s32>(aut) * p.autonomy_resist,
					     static_cast<eng::s16>(50)));
	margin = static_cast<eng::s16>(
		margin - eng::math::div_wide(static_cast<eng::s32>(def) * p.deference_accept,
					     static_cast<eng::s16>(128)));
	margin = static_cast<eng::s16>(margin - u8_scale(mind.emotions.fear, p.fear_submission));
	if (margin < 0) {
		margin = 0;
	}
	return static_cast<eng::s16>(other_power) >=
	       static_cast<eng::s16>(self_power) + margin;
}

/// Presión de someterse `[0,1000]`: alta si el otro es claramente superior, si hay
/// deferencia/miedo y baja autonomía.
[[nodiscard]] constexpr Score submission_score(eng::u8 self_power, eng::u8 other_power,
					       const Personality& personality, const Mind& mind,
					       const HierarchyParams& p = HierarchyParams {}) noexcept {
	if (other_power <= self_power) {
		return 0;
	}
	const eng::u16 gap = static_cast<eng::u16>(other_power - self_power);
	eng::s32 s = static_cast<eng::s32>(gap) * 8; // 0..2040
	s += u8_scale(mind.deference, 40u) * 4;
	s += u8_scale(mind.emotions.fear, p.fear_submission) * 4;
	s -= u8_scale(personality.autonomy, 30u) * 4;
	if (s < 0) {
		return 0;
	}
	return s > 1000 ? static_cast<Score>(1000) : static_cast<Score>(s);
}

/// Presión de resistir `[0,1000]`: alta con autonomía e ira, moderada por prudencia ante
/// un rival mucho más fuerte.
[[nodiscard]] constexpr Score defiance_score(eng::u8 self_power, eng::u8 other_power,
					     const Personality& personality, const Mind& mind,
					     const HierarchyParams& p = HierarchyParams {}) noexcept {
	eng::s32 s = static_cast<eng::s32>(personality.autonomy) * 8; // 0..800
	s += u8_scale(mind.emotions.anger, p.anger_defiance) * 4;
	s -= u8_scale(mind.deference, 30u) * 4;
	if (other_power > self_power) {
		const eng::u16 gap = static_cast<eng::u16>(other_power - self_power);
		s -= static_cast<eng::s32>(
			div_u16(static_cast<eng::u16>(gap * p.prudence), 16u));
	} else {
		s += static_cast<eng::s32>(self_power - other_power) * 4; // ventaja: más ganas
	}
	if (s < 0) {
		return 0;
	}
	return s > 1000 ? static_cast<Score>(1000) : static_cast<Score>(s);
}

/// Banda de poder (0..4) para reglas sociales y presentación.
[[nodiscard]] constexpr eng::u8 rank_band(eng::u8 power) noexcept {
	if (power < 20u) {
		return 0u;
	}
	if (power < 40u) {
		return 1u;
	}
	if (power < 60u) {
		return 2u;
	}
	if (power < 80u) {
		return 3u;
	}
	return 4u;
}

/// Poder de una criatura a partir de su personalidad y salud `[0,100]`.
[[nodiscard]] constexpr eng::u8 creature_power(const Personality& personality,
					       eng::u8 condition,
					       const HierarchyParams& p = HierarchyParams {}) noexcept {
	return contest_power(personality.dominance, personality.aggression, condition, p);
}

} // namespace eng::sim
