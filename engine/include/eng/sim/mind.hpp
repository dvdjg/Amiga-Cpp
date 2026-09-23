#pragma once

/// \file mind.hpp
/// **Simulación de la mente** de una criatura (`eng::sim`): estado afectivo, pulsiones
/// (autonomía/deferencia) y memoria episódica. Las necesidades dicen *qué falta*; la
/// mente dice *cómo se siente* la criatura al respecto, y ese estado modula la decisión.
///
/// Doce ejes afectivos, todos `u8` en `[0, 255]`:
/// `fear`, `anger`, `joy`, `sadness`, `curiosity`, `compassion`, `love`, `hatred`,
/// `envy`, `jealousy`, `apathy`, `cordiality`.
///
/// Dos pulsiones estables (no emociones):
/// - `autonomy`: deseo de **libertad** (no someterse).
/// - `deference`: disposición a **someterse** a un dominante cuando conviene.
///
/// La dinámica es un modelo lineal **paramétrico** (`AffectParams`): cada eje tiende por
/// pasos hacia un objetivo derivado de las necesidades, la personalidad, la memoria y los
/// propios afectos (realimentación). Cambiar los pesos cambia la "psicología" sin tocar el
/// código. Todo entero y determinista (apto para el 68000).
///
/// La memoria guarda hasta `kMaxMemoryEvents` recuerdos con actor, intensidad y edad, y
/// los va olvidando. Los recuerdos negativos (traición, sometimiento, robo) alimentan ira,
/// odio, envidia o tristeza; los positivos (ayuda, enseñanza, cortejo) alimentan amor,
/// compasión y cordialidad.
///
/// Con `SimTraits::emotions == false` la mente no se actualiza y la decisión ignora los
/// afectos (el compilador elimina el camino).
///
/// Referencia de diseño: los "módulos de utilidad" y la memoria de *Rain World*
/// (`docs/engine/architecture/GAME_AI_LIBRARY.md`).
/// Verificación: HOST-152 (afectos) y HOST-154 (jerarquía/genética).

#include <eng/core/types/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/needs.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Ejes afectivos.
enum class Emotion : eng::u8 {
	Fear = 0,
	Anger = 1,
	Joy = 2,
	Sadness = 3,
	Curiosity = 4,
	Compassion = 5,
	Love = 6,
	Hatred = 7,
	Envy = 8,
	Jealousy = 9,
	Apathy = 10,
	Cordiality = 11,
	Count = 12,
};

inline constexpr eng::usize emotion_count = static_cast<eng::usize>(Emotion::Count);

/// Estado afectivo de un byte por eje (0 = nada, 255 = extremo).
struct Emotions {
	eng::u8 fear = 0;
	eng::u8 anger = 0;
	eng::u8 joy = 128u;
	eng::u8 sadness = 0;
	eng::u8 curiosity = 128u;
	eng::u8 compassion = 100u;
	eng::u8 love = 60u;
	eng::u8 hatred = 0;
	eng::u8 envy = 0;
	eng::u8 jealousy = 0;
	eng::u8 apathy = 0;
	eng::u8 cordiality = 128u;

	[[nodiscard]] constexpr eng::u8& at(Emotion e) noexcept {
		switch (e) {
			case Emotion::Fear: return fear;
			case Emotion::Anger: return anger;
			case Emotion::Joy: return joy;
			case Emotion::Sadness: return sadness;
			case Emotion::Curiosity: return curiosity;
			case Emotion::Compassion: return compassion;
			case Emotion::Love: return love;
			case Emotion::Hatred: return hatred;
			case Emotion::Envy: return envy;
			case Emotion::Jealousy: return jealousy;
			case Emotion::Apathy: return apathy;
			case Emotion::Cordiality: return cordiality;
			default: return joy;
		}
	}
	[[nodiscard]] constexpr eng::u8 at(Emotion e) const noexcept {
		switch (e) {
			case Emotion::Fear: return fear;
			case Emotion::Anger: return anger;
			case Emotion::Joy: return joy;
			case Emotion::Sadness: return sadness;
			case Emotion::Curiosity: return curiosity;
			case Emotion::Compassion: return compassion;
			case Emotion::Love: return love;
			case Emotion::Hatred: return hatred;
			case Emotion::Envy: return envy;
			case Emotion::Jealousy: return jealousy;
			case Emotion::Apathy: return apathy;
			case Emotion::Cordiality: return cordiality;
			default: return joy;
		}
	}
};

/// Tipo de recuerdo episódico (memoria social y de aprendizaje).
enum class MemoryKind : eng::u8 {
	Ate = 0,          ///< comió (positivo)
	WasHurt = 1,      ///< recibió daño (negativo)
	WasHelped = 2,    ///< recibió ayuda (positivo)
	Robbed = 3,       ///< le robaron (negativo)
	Escaped = 4,      ///< escapó de un peligro (positivo)
	SawEnemy = 5,     ///< avistó un enemigo (negativo, miedo)
	SawAlly = 6,      ///< se encontró con un aliado (positivo)
	Played = 7,       ///< interacción lúdica (positivo)
	WasTaught = 8,    ///< le enseñaron algo (positivo, conocimiento)
	WasBetrayed = 9,  ///< traición de un aliado (muy negativo)
	WasSubjugated = 10, ///< fue sometido (negativo, humillación)
	Dominated = 11,   ///< sometió a otro (positivo para el dominante)
	Courted = 12,     ///< cortejo recibido (positivo)
	Rejected = 13,    ///< rechazo afectivo (negativo)
	WasPunished = 14, ///< castigo del grupo (negativo, disuasorio)
	SharedFood = 15,  ///< compartió/recibió comida (positivo, vínculo)
	Count = 16,
};

/// Valencia del recuerdo en `[-2, +2]`: cuánto tiñe el estado afectivo.
[[nodiscard]] constexpr eng::s8 memory_valence(MemoryKind k) noexcept {
	switch (k) {
		case MemoryKind::Ate: return 1;
		case MemoryKind::WasHurt: return -2;
		case MemoryKind::WasHelped: return 2;
		case MemoryKind::Robbed: return -2;
		case MemoryKind::Escaped: return 2;
		case MemoryKind::SawEnemy: return -1;
		case MemoryKind::SawAlly: return 1;
		case MemoryKind::Played: return 1;
		case MemoryKind::WasTaught: return 2;
		case MemoryKind::WasBetrayed: return -2;
		case MemoryKind::WasSubjugated: return -2;
		case MemoryKind::Dominated: return 1;
		case MemoryKind::Courted: return 1;
		case MemoryKind::Rejected: return -1;
		case MemoryKind::WasPunished: return -1;
		case MemoryKind::SharedFood: return 1;
		default: return 0;
	}
}

/// ¿El recuerdo es social (implica a un `actor` concreto)?
[[nodiscard]] constexpr bool memory_is_social(MemoryKind k) noexcept {
	switch (k) {
		case MemoryKind::Ate:
		case MemoryKind::Escaped:
		case MemoryKind::SawEnemy:
			return false;
		default:
			return true;
	}
}

/// Un recuerdo: qué pasó, con quién y con qué intensidad (0..255). `age` son ticks.
struct MemoryEvent {
	MemoryKind kind = MemoryKind::Ate;
	EntityId actor = no_entity;
	eng::u8 intensity = 0;
	eng::u8 age = 0;
};

/// Pesos del modelo afectivo. Todos porcentuales; cambiarlos reajusta la "psicología".
struct AffectParams {
	eng::u8 exposure_fear = 80u;      ///< cuánto del estrés ambiental se vuelve miedo
	eng::u8 injury_anger = 90u;       ///< herida -> ira
	eng::u8 negative_anger = 60u;     ///< recuerdos negativos -> ira
	eng::u8 negative_sadness = 70u;   ///< recuerdos negativos -> tristeza
	eng::u8 negative_hatred = 60u;    ///< recuerdos negativos -> odio
	eng::u8 negative_envy = 55u;      ///< recuerdos negativos -> envidia
	eng::u8 positive_love = 60u;      ///< recuerdos positivos -> amor
	eng::u8 empathy_compassion = 80u; ///< empatía -> compasión
	eng::u8 love_jealousy = 50u;      ///< amor -> celos
	eng::u8 sadness_apathy = 70u;     ///< tristeza -> apatía
	eng::u8 fatigue_apathy = 40u;     ///< cansancio -> apatía
	eng::u8 soc_cordiality = 70u;     ///< sociabilidad -> cordialidad
	eng::u8 loyalty_deference = 60u;  ///< lealtad -> disposición a someterse
	eng::u8 fear_deference = 50u;     ///< miedo -> someterse
	eng::u8 anger_defiance = 45u;     ///< ira -> resistencia (menos deferencia)
	eng::u8 step = 12u;               ///< paso de aproximación por actualización
};

/// Acerca `value` a `target` un `step`, sin pasarse.
inline constexpr void approach(eng::u8& value, eng::u8 target, eng::u8 step) noexcept {
	if (value < target) {
		value = u8_sat_add(value, step);
		if (value > target) {
			value = target;
		}
	} else if (value > target) {
		value = u8_sat_sub(value, step);
		if (value < target) {
			value = target;
		}
	}
}

/// Recorta un valor de `u16` a `[0,255]`.
[[nodiscard]] constexpr eng::u8 clamp_u8(eng::u16 v) noexcept {
	return v > 255u ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(v);
}

/// Recorta un valor con signo a `[0,255]` (para expresiones con restas).
[[nodiscard]] constexpr eng::u8 clamp_i32(eng::s32 v) noexcept {
	if (v < 0) {
		return 0u;
	}
	return v > 255 ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(v);
}

/// Modificador porcentual `[-100,+100]` de un eje afectivo respecto a su punto neutro
/// (128). Un miedo alto da modificador positivo; uno bajo, negativo.
[[nodiscard]] constexpr eng::s16 emotion_mod(eng::u8 emotion) noexcept {
	const eng::s16 centered = static_cast<eng::s16>(emotion) - 128;
	const eng::s16 scaled = static_cast<eng::s16>(centered * 2);
	if (scaled > 100) {
		return 100;
	}
	if (scaled < -100) {
		return -100;
	}
	return scaled;
}

/// Mente de una criatura: afectos, pulsiones, memoria y agregados.
struct Mind {
	Emotions emotions {};
	eng::u8 autonomy = 128u;  ///< deseo de libertad (alto = no someterse)
	eng::u8 deference = 128u; ///< disposición a someterse (alto = obedecer)
	eng::u8 stress = 0;
	eng::u8 morale = 128u;
	eng::util::StaticVector<MemoryEvent, kMaxMemoryEvents> recent {};

	/// Registra un recuerdo. Envejece los anteriores y, si está lleno, descarta el más
	/// antiguo.
	constexpr void remember(MemoryKind kind, EntityId actor, eng::u8 intensity) noexcept {
		for (eng::usize i = 0; i < recent.size(); ++i) {
			if (recent[i].age < 255u) {
				++recent[i].age;
			}
		}
		if (recent.full()) {
			eng::usize oldest = 0u;
			for (eng::usize i = 1; i < recent.size(); ++i) {
				if (recent[i].age > recent[oldest].age) {
					oldest = i;
				}
			}
			recent[oldest] = MemoryEvent {kind, actor, intensity, 0};
			return;
		}
		(void)recent.push_back(MemoryEvent {kind, actor, intensity, 0});
	}

	/// ¿Hay un recuerdo reciente de este tipo?
	[[nodiscard]] constexpr bool recalls(MemoryKind kind) const noexcept {
		for (eng::usize i = 0; i < recent.size(); ++i) {
			if (recent[i].kind == kind) {
				return true;
			}
		}
		return false;
	}

	/// Dirección afectiva hacia `actor`: suma de valencias de los recuerdos que lo
	/// implican, en `[-64, 64]`. Positivo = afecto, negativo = rencor.
	[[nodiscard]] constexpr eng::s16 attitude_toward(EntityId actor) const noexcept {
		if (actor == no_entity) {
			return 0;
		}
		eng::s16 sum = 0;
		for (eng::usize i = 0; i < recent.size(); ++i) {
			if (recent[i].actor == actor) {
				sum = static_cast<eng::s16>(
					sum + static_cast<eng::s16>(memory_valence(recent[i].kind)));
			}
		}
		if (sum > 64) {
			sum = 64;
		}
		if (sum < -64) {
			sum = -64;
		}
		return sum;
	}

	/// Balance de valencias (frescura e intensidad ponderan). Positivo = bienestar.
	[[nodiscard]] constexpr eng::s16 memory_balance() const noexcept {
		eng::s16 sum = 0;
		for (eng::usize i = 0; i < recent.size(); ++i) {
			const eng::s16 v = static_cast<eng::s16>(memory_valence(recent[i].kind));
			const eng::s16 fresh =
				recent[i].age < 20u ? static_cast<eng::s16>(20 - recent[i].age) : 1;
			const eng::s16 weight = static_cast<eng::s16>(
				div_u16(static_cast<eng::u16>(recent[i].intensity), 64u) + 1u);
			sum = static_cast<eng::s16>(sum + v * (fresh / 4 + 1) * weight);
		}
		return sum;
	}

	/// Avanza la mente un frame. Deriva cada afecto y pulsión hacia su objetivo y
	/// recalcula estrés y moral. Toda la fórmula es paramétrica (`AffectParams`).
	constexpr void update(const Needs& needs, const Personality& personality,
			      const AffectParams& p = AffectParams {}) noexcept {
		const eng::s16 balance = memory_balance();
		const eng::u8 pos = balance > 0 ? clamp_i32(static_cast<eng::s32>(balance * 8)) : 0u;
		const eng::u8 neg = balance < 0 ? clamp_i32(static_cast<eng::s32>(-balance * 8)) : 0u;
		const eng::u8 drive = static_cast<eng::u8>(
			div_u16(static_cast<eng::u16>(needs.hunger) + needs.fatigue, 2u));

		const eng::u8 fear_t = u8_max(
			needs.fear,
			clamp_i32(static_cast<eng::s32>(u8_scale(needs.exposure, p.exposure_fear)) +
				 u8_scale(neg, 40u)));
		const eng::u8 anger_t = clamp_i32(
			static_cast<eng::s32>(u8_max(u8_scale(needs.injury, p.injury_anger),
						     u8_scale(neg, p.negative_anger))) +
			u8_scale(personality.aggression, 30u));
		const eng::u8 sadness_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(neg, p.negative_sadness) +
					      u8_scale(needs.fatigue, 30u)) -
			u8_scale(emotions.joy, 50u));
		const eng::u8 joy_t = clamp_i32(
			static_cast<eng::s32>(static_cast<eng::u16>(255u) - drive + u8_scale(pos, 50u)) -
			u8_scale(emotions.sadness, 50u));
		const eng::u8 compassion_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(personality.empathy, p.empathy_compassion) +
					      u8_scale(pos, 40u)) -
			u8_scale(emotions.hatred, 40u));
		const eng::u8 love_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(pos, p.positive_love) +
					      u8_scale(personality.loyalty, 40u)) -
			u8_scale(emotions.fear, 40u));
		const eng::u8 hatred_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(neg, p.negative_hatred) +
					      u8_scale(needs.injury, 50u)) -
			u8_scale(emotions.compassion, 50u));
		const eng::u8 envy_t = clamp_i32(static_cast<eng::s32>(u8_scale(neg, p.negative_envy)) +
						u8_scale(personality.dominance, 30u));
		const eng::u8 jealousy_t =
			clamp_i32(static_cast<eng::s32>(u8_scale(emotions.love, p.love_jealousy)) +
				 u8_scale(neg, 40u));
		const eng::u8 apathy_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(emotions.sadness, p.sadness_apathy) +
					      u8_scale(needs.fatigue, p.fatigue_apathy)) -
			u8_scale(emotions.joy, 60u));
		const eng::u8 cordiality_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(personality.sociability, p.soc_cordiality) +
					      u8_scale(emotions.joy, 40u)) -
			u8_scale(emotions.hatred, 60u));
		const eng::u8 curiosity_t = clamp_i32(
			static_cast<eng::s32>(u8_scale(personality.curiosity, 100u)) -
			u8_scale(emotions.fear, 70u));

		approach(emotions.fear, fear_t, p.step);
		approach(emotions.anger, anger_t, p.step);
		approach(emotions.sadness, sadness_t, p.step);
		approach(emotions.joy, joy_t, p.step);
		approach(emotions.compassion, compassion_t, p.step);
		approach(emotions.love, love_t, p.step);
		approach(emotions.hatred, hatred_t, p.step);
		approach(emotions.envy, envy_t, p.step);
		approach(emotions.jealousy, jealousy_t, p.step);
		approach(emotions.apathy, apathy_t, p.step);
		approach(emotions.cordiality, cordiality_t, p.step);
		approach(emotions.curiosity, curiosity_t, p.step);

		// Pulsiones: la autonomía es un rasgo estable; la deferencia depende de lealtad,
		// miedo (someterse por temor) e ira (resistirse).
		const eng::u8 autonomy_t = personality.autonomy;
		const eng::u8 deference_t =
			clamp_i32(static_cast<eng::s32>(u8_scale(personality.loyalty, p.loyalty_deference) +
						       u8_scale(emotions.fear, p.fear_deference)) -
				 u8_scale(emotions.anger, p.anger_defiance));
		approach(autonomy, autonomy_t, p.step);
		approach(deference, deference_t, p.step);

		// Estrés y moral: agregados de un byte.
		const eng::u16 stress_sum = static_cast<eng::u16>(
			static_cast<eng::u16>(emotions.fear) + emotions.anger + needs.hunger +
			needs.injury + needs.exposure);
		const eng::u16 stress_t = div_u16(stress_sum, 4u);
		approach(stress, clamp_u8(stress_t), static_cast<eng::u8>(p.step + 4u));
		morale = clamp_i32(static_cast<eng::s32>(
			static_cast<eng::u16>(128u) + u8_scale(emotions.joy, 40u) +
			u8_scale(emotions.cordiality, 20u) - u8_scale(emotions.sadness, 40u) -
			u8_scale(emotions.apathy, 30u) - u8_scale(emotions.fear, 20u)));
	}

	/// Modificador afectivo genérico, para `apply_mod`.
	[[nodiscard]] constexpr eng::s16 mod(Emotion e) const noexcept {
		return emotion_mod(emotions.at(e));
	}

	[[nodiscard]] constexpr eng::s16 fear_mod() const noexcept { return mod(Emotion::Fear); }
	[[nodiscard]] constexpr eng::s16 anger_mod() const noexcept { return mod(Emotion::Anger); }
	[[nodiscard]] constexpr eng::s16 joy_mod() const noexcept { return mod(Emotion::Joy); }
	[[nodiscard]] constexpr eng::s16 sadness_mod() const noexcept {
		return mod(Emotion::Sadness);
	}
	[[nodiscard]] constexpr eng::s16 curiosity_mod() const noexcept {
		return mod(Emotion::Curiosity);
	}
	[[nodiscard]] constexpr eng::s16 compassion_mod() const noexcept {
		return mod(Emotion::Compassion);
	}
	[[nodiscard]] constexpr eng::s16 love_mod() const noexcept { return mod(Emotion::Love); }
	[[nodiscard]] constexpr eng::s16 hatred_mod() const noexcept {
		return mod(Emotion::Hatred);
	}
	[[nodiscard]] constexpr eng::s16 envy_mod() const noexcept { return mod(Emotion::Envy); }
	[[nodiscard]] constexpr eng::s16 jealousy_mod() const noexcept {
		return mod(Emotion::Jealousy);
	}
	[[nodiscard]] constexpr eng::s16 apathy_mod() const noexcept {
		return mod(Emotion::Apathy);
	}
	[[nodiscard]] constexpr eng::s16 cordiality_mod() const noexcept {
		return mod(Emotion::Cordiality);
	}
};

} // namespace eng::sim
