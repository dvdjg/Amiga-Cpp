#pragma once

/// \file behavior.hpp
/// **Decisión por utilidad** de una criatura (`eng::sim`): cada comportamiento propone
/// una puntuación (`Score`, 0..1000) a partir de las necesidades, la personalidad, los
/// trackers, el conocimiento y (si `Traits::emotions`) el estado afectivo; gana el de
/// mayor puntuación, con **histéresis** para no oscilar.
///
/// Es el equivalente simplificado de los "módulos de utilidad" de *Rain World*: el GOAP
/// (`planner.hpp`) se reserva para planes ocasionales y esta utilidad es la que corre cada
/// tick. Los comportamientos concretos los ejecuta el juego: aquí solo se **elige**.
///
/// Las funciones son **plantillas sobre la criatura** (duck typing: basta con que tenga
/// `needs`, `personality`, `trackers`, `x`, `y` y, si procede, `mind`/`knowledge`), de
/// modo que el mismo algoritmo sirve a cualquier proyección o modelo de entidad. Los pesos
/// por comportamiento (`BehaviorWeights`) permiten reajustar la "balanza" sin tocar el
/// algoritmo.
///
/// Verificación: HOST-152 (decision), HOST-154 (afectos/jerarquía).

#include <eng/ai/decision/utility.hpp>
#include <eng/core/ptr.hpp>
#include <eng/core/random.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/bit.hpp>
#include <eng/sim/hierarchy.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/relationship.hpp>
#include <eng/sim/senses.hpp>
#include <eng/sim/tracker.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Comportamientos de alto nivel que compiten por el control de la criatura.
enum class Behavior : eng::u8 {
	Idle = 0,        ///< quieta, sin objetivo
	Wander = 1,      ///< deambular/explorar
	Hunt = 2,        ///< perseguir presa
	Flee = 3,        ///< huir de amenaza
	SeekFood = 4,    ///< buscar/ir a comida
	Sleep = 5,       ///< dormir/descansar
	Socialize = 6,   ///< interactuar con el grupo
	SeekShelter = 7, ///< buscar refugio (peligro ambiental)
	Tend = 8,        ///< atender a la cría/familiar
	Help = 9,        ///< ayudar a un aliado (compasión)
	Court = 10,      ///< cortejar/reproducir (amor)
	Submit = 11,     ///< someterse a un dominante (deferencia)
	Defy = 12,       ///< resistir/rebelarse (autonomía)
	Teach = 13,      ///< compartir conocimiento
	Forage = 14,     ///< trabajar para la colonia (enjambre)
	Avenge = 15,     ///< confrontar a un rival/agresor concreto (odio/celos)
	Count = 16,
};

inline constexpr eng::usize behavior_count = static_cast<eng::usize>(Behavior::Count);

/// Contexto externo que no vive en la criatura (clima, refugio, dominancia, saber).
struct BehaviorContext {
	bool environment_severe = false; ///< peligro ambiental activo
	bool has_den = false;            ///< tiene un refugio asignado
	bool has_dominant = false;       ///< hay un individuo dominante cerca
	bool mature = true;              ///< ha alcanzado la madurez (puede cortejar)
	eng::u8 self_power = 0;          ///< poder propio (jerarquía)
	eng::u8 dominant_power = 0;      ///< poder del dominante observado
	eng::u8 knowledge_confidence = 0;///< confianza de lo que podría enseñar (0..255)
};

/// Pesos por comportamiento (100 = neutro). Reajustan la balanza sin cambiar el código.
struct BehaviorWeights {
	eng::u8 base[behavior_count];
	constexpr BehaviorWeights() noexcept {
		for (eng::usize i = 0; i < behavior_count; ++i) {
			base[i] = 100u;
		}
	}
};

/// Puntuaciones de todos los comportamientos (una por `Behavior`).
struct BehaviorScores {
	Score v[behavior_count] {};

	[[nodiscard]] constexpr Score& operator[](Behavior b) noexcept {
		return v[static_cast<eng::usize>(b)];
	}
	[[nodiscard]] constexpr Score operator[](Behavior b) const noexcept {
		return v[static_cast<eng::usize>(b)];
	}
};

/// Convierte una presión de necesidad `[0,255]` a la escala `[0,1000]` de la utilidad.
[[nodiscard]] constexpr Score pressure_score(eng::u8 pressure) noexcept {
	return static_cast<Score>(pressure) * 4;
}

/// Cercanía de un tracker a la criatura en `[0,255]` (255 = encima).
[[nodiscard]] constexpr eng::u8 proximity(const Tracker& t, eng::s16 x, eng::s16 y) noexcept {
	const eng::u16 d = manhattan(t.x, t.y, x, y);
	return d >= 255u ? static_cast<eng::u8>(0u) : static_cast<eng::u8>(255u - d);
}

/// Aplica el peso de un comportamiento: 100 no cambia, 50 lo reduce a la mitad, 150 lo
/// aumenta un 50 %.
[[nodiscard]] constexpr Score weighted(Score score, eng::u8 weight) noexcept {
	return apply_mod(score, static_cast<eng::s16>(weight) - 100);
}

/// Pesos de la **atención**: cuánto cuentan la saliencia y cada modalidad extra al elegir
/// a qué prestar atención entre los trackers.
struct AttentionWeights {
	eng::u8 salience_weight = 100u; ///< % de la saliencia que suma
	eng::u8 modality_bonus = 20u;   ///< puntos por cada sentido que detectó al objetivo
};

/// Puntuación de atención de un tracker: confianza + saliencia + modalidades. Cuantos más
/// sentidos lo detectan (p. ej. lo ves Y lo oyes), más pesa frente a una detección débil.
[[nodiscard]] constexpr Score attention_score(const Tracker& t,
					      const AttentionWeights& w = AttentionWeights {}) noexcept {
	const int mods = eng::util::popcount(static_cast<eng::u32>(t.modalities));
	return static_cast<Score>(t.confidence) +
	       static_cast<Score>(u8_scale(t.salience, w.salience_weight)) +
	       static_cast<Score>(mods * static_cast<int>(w.modality_bonus));
}

/// Tracker de un tipo con mayor atención (confianza + saliencia + multimodalidad).
template <eng::usize N>
[[nodiscard]] constexpr eng::Ref<const Tracker> best_attention_tracker(
	const TrackerList<N>& trackers, TrackerKind kind,
	const AttentionWeights& w = AttentionWeights {}) noexcept {
	eng::Ref<const Tracker> best {};
	Score best_score = -1;
	for (eng::usize i = 0; i < trackers.size(); ++i) {
		const Tracker& t = trackers[i];
		if (t.kind != kind) {
			continue;
		}
		const Score s = attention_score(t, w);
		if (!best.valid() || s > best_score) {
			best = eng::Ref<const Tracker>(&t);
			best_score = s;
		}
	}
	return best;
}

/// Igual que el anterior, partiendo de una criatura (usa su lista de trackers).
template <class C>
[[nodiscard]] constexpr eng::Ref<const Tracker> best_attention_tracker(
	const C& c, TrackerKind kind,
	const AttentionWeights& w = AttentionWeights {}) noexcept {
	return best_attention_tracker(c.trackers, kind, w);
}

/// Tracker de un tipo cuyo target tiene el mejor vínculo (`bond_score`). Permite elegir
/// **a quién** ayudar, cortejar o confrontar; `prefer_positive` busca aliado (vínculo
/// positivo) o enemigo (negativo).
template <class C>
[[nodiscard]] constexpr eng::Ref<const Tracker> best_bonded_tracker(const C& c, TrackerKind kind,
								    bool prefer_positive) noexcept {
	eng::Ref<const Tracker> best {};
	eng::s32 best_score = prefer_positive ? -100000 : 100000;
	for (eng::usize i = 0; i < c.trackers.size(); ++i) {
		const Tracker& t = c.trackers[i];
		if (t.kind != kind) {
			continue;
		}
		const eng::s32 score = bond_score(c.relationships, t.target);
		if (prefer_positive ? score > best_score : score < best_score) {
			best_score = score;
			best = eng::Ref<const Tracker>(&t);
		}
	}
	return best;
}

/// Calcula la puntuación de todos los comportamientos. No usa aleatoriedad (el ruido de
/// decisión entra en `choose_behavior`), de modo que dos evaluaciones del mismo estado
/// dan el mismo resultado.
template <class Traits, class C>
constexpr void score_behaviors(const C& c, const BehaviorContext& ctx, BehaviorScores& out,
			       const BehaviorWeights& weights = BehaviorWeights {}) noexcept {
	const eng::s16 px = c.x;
	const eng::s16 py = c.y;

	// --- Huir: amenazas percibidas + nerviosismo - valentía (+ miedo afectivo) ---
	{
		Score base = 0;
		if (auto t = best_attention_tracker(c, TrackerKind::Threat); t.valid()) {
			eng::ai::Utility u;
			u.add(t->confidence, 3);
			u.add(proximity(*t, px, py), 2);
			base = static_cast<Score>(u.score());
		}
		if (base > 0) {
			eng::s16 mod = static_cast<eng::s16>(c.personality.nervousness_mod() -
							     c.personality.bravery_mod());
			if constexpr (Traits::emotions) {
				// Miedo unipolar: alto asusta, cero no penaliza.
				mod = static_cast<eng::s16>(mod + u8_scale(c.mind.emotions.fear, 60u));
			}
			base = apply_mod(base, mod);
		}
		out[Behavior::Flee] = weighted(base, weights.base[static_cast<eng::usize>(Behavior::Flee)]);
	}

	// --- Cazar: hambre + presa percibida; la agresividad y la ira empujan ---
	{
		Score base = 0;
		if (auto prey = best_attention_tracker(c, TrackerKind::Prey);
		    prey.valid() && c.needs.hunger > 40u) {
			eng::ai::Utility u;
			u.add(pressure_score(c.needs.hunger), 3);
			u.add(prey->confidence, 2);
			u.add(proximity(*prey, px, py), 1);
			base = static_cast<Score>(u.score());
			eng::s16 mod = c.personality.aggression_mod();
			if constexpr (Traits::emotions) {
				mod = static_cast<eng::s16>(mod + c.mind.anger_mod() / 2);
			}
			base = apply_mod(base, mod);
		}
		out[Behavior::Hunt] = weighted(base, weights.base[static_cast<eng::usize>(Behavior::Hunt)]);
	}

	// --- Buscar comida: el hambre por sí sola ya es motivo; con un item visto, más ---
	{
		eng::ai::Utility u;
		u.add(pressure_score(c.needs.hunger), 3);
		if (auto food = best_attention_tracker(c, TrackerKind::Item);
		    food.valid()) {
			u.add(food->confidence, 2);
			u.add(proximity(*food, px, py), 1);
		}
		Score base = static_cast<Score>(u.score());
		base = apply_mod(base, static_cast<eng::s16>(c.personality.curiosity_mod() / 2));
		out[Behavior::SeekFood] =
			weighted(base, weights.base[static_cast<eng::usize>(Behavior::SeekFood)]);
	}

	// --- Dormir: el cansancio manda; el miedo lo penaliza; la apatía lo empuja ---
	{
		Score base = pressure_score(c.needs.fatigue);
		if constexpr (Traits::emotions) {
			base = apply_mod(base, static_cast<eng::s16>(-u8_scale(c.mind.emotions.fear, 50u)));
			base = apply_mod(base, static_cast<eng::s16>(u8_scale(c.mind.emotions.apathy, 60u)));
		}
		out[Behavior::Sleep] = weighted(base, weights.base[static_cast<eng::usize>(Behavior::Sleep)]);
	}

	// --- Socializar: presión social + aliado percibido (solo con sociedad) ---
	if constexpr (Traits::society) {
		eng::ai::Utility u;
		u.add(pressure_score(c.needs.social), 3);
		if (auto ally = best_attention_tracker(c, TrackerKind::Friend);
		    ally.valid()) {
			u.add(ally->confidence, 2);
			u.add(proximity(*ally, px, py), 1);
		}
		Score base = static_cast<Score>(u.score());
		base = apply_mod(base, c.personality.sociability_mod());
		if constexpr (Traits::emotions) {
			base = apply_mod(base, static_cast<eng::s16>(c.mind.cordiality_mod() / 2));
		}
		out[Behavior::Socialize] =
			weighted(base, weights.base[static_cast<eng::usize>(Behavior::Socialize)]);
	} else {
		out[Behavior::Socialize] = 0;
	}

	// --- Buscar refugio: estrés ambiental (lluvia/frío/calor...) o herida grave ---
	{
		Score base = 0;
		if (ctx.has_den) {
			base = pressure_score(c.needs.exposure);
			if (ctx.environment_severe) {
				base = base > 700 ? base : static_cast<Score>(700);
			}
			if (c.needs.injury > 160u) {
				base = base > 600 ? base : static_cast<Score>(600);
			}
			if constexpr (Traits::emotions) {
				base = apply_mod(base, static_cast<eng::s16>(u8_scale(c.mind.emotions.fear, 40u)));
			}
		}
		out[Behavior::SeekShelter] =
			weighted(base, weights.base[static_cast<eng::usize>(Behavior::SeekShelter)]);
	}

	// --- Deambular: línea base exploratoria; la curiosidad la sube ---
	{
		Score base =
			static_cast<Score>(200u + static_cast<eng::u16>(c.personality.curiosity) * 2u);
		if constexpr (Traits::emotions) {
			base = apply_mod(base, static_cast<eng::s16>(c.mind.curiosity_mod() / 2));
		}
		out[Behavior::Wander] = weighted(base, weights.base[static_cast<eng::usize>(Behavior::Wander)]);
	}

	// --- Quedarse quieta: la opción barata cuando no apremia nada ---
	out[Behavior::Idle] = weighted(
		static_cast<Score>(140u + static_cast<eng::u16>(100u - c.personality.curiosity)),
		weights.base[static_cast<eng::usize>(Behavior::Idle)]);

	if constexpr (Traits::society) {
		// --- Atender a la cría: percibir un familiar la convierte en prioridad ---
		{
			eng::ai::Utility u;
			u.add(pressure_score(c.needs.social), 1);
			bool has_kin = false;
			if (auto kin = best_attention_tracker(c, TrackerKind::Kin);
			    kin.valid()) {
				u.add(kin->confidence, 3);
				u.add(proximity(*kin, px, py), 2);
				has_kin = true;
			}
			Score base = static_cast<Score>(u.score());
			if (has_kin) {
				// Un dependiente percibido domina la decisión (cuidado parental).
				base = base > 600 ? base : static_cast<Score>(600);
			}
			base = apply_mod(base, c.personality.empathy_mod());
			out[Behavior::Tend] =
				weighted(base, weights.base[static_cast<eng::usize>(Behavior::Tend)]);
		}

		// --- Ayudar: compasión + aliado con vínculo positivo ---
		{
			eng::ai::Utility u;
			if constexpr (Traits::emotions) {
				u.add(u8_scale(c.mind.emotions.compassion, 100u), 3);
			}
			auto friend_t = best_bonded_tracker(c, TrackerKind::Friend, true);
			if (!friend_t.valid()) {
				friend_t = best_bonded_tracker(c, TrackerKind::Kin, true);
			}
			if (friend_t.valid()) {
				const eng::s16 bond = bond_score(c.relationships, friend_t->target);
				if (bond > 0) {
					u.add(friend_t->confidence, 2);
					u.add(proximity(*friend_t, px, py), 1);
					u.add(static_cast<eng::s32>(bond) + 100, 2); // vínculo alto pesa
				}
			}
			Score base = static_cast<Score>(u.score());
			base = apply_mod(base, c.personality.empathy_mod());
			out[Behavior::Help] =
				weighted(base, weights.base[static_cast<eng::usize>(Behavior::Help)]);
		}

		// --- Cortejar: solo en la madurez; amor + pareja con vínculo positivo ---
		{
			Score base = 0;
			if (ctx.mature) {
				eng::ai::Utility u;
				if constexpr (Traits::emotions) {
					u.add(u8_scale(c.mind.emotions.love, 100u), 3);
				}
				auto mate = best_bonded_tracker(c, TrackerKind::Mate, true);
				if (!mate.valid()) {
					mate = best_bonded_tracker(c, TrackerKind::Friend, true);
				}
				if (mate.valid()) {
					const eng::s16 bond = bond_score(c.relationships, mate->target);
					if (bond >= 0) {
						u.add(mate->confidence, 1);
						u.add(proximity(*mate, px, py), 1);
					}
				}
				base = static_cast<Score>(u.score());
				base = apply_mod(base, c.personality.loyalty_mod());
			}
			out[Behavior::Court] =
				weighted(base, weights.base[static_cast<eng::usize>(Behavior::Court)]);
		}

		// --- Venganza selectiva: confrontar a un rival/agresor concreto ---
		{
			Score base = 0;
			auto foe = best_bonded_tracker(c, TrackerKind::Rival, false);
			if (!foe.valid()) {
				foe = best_bonded_tracker(c, TrackerKind::Threat, false);
			}
			if (foe.valid()) {
				const eng::s16 bond = bond_score(c.relationships, foe->target);
				if (bond < 0) {
					eng::ai::Utility u;
					if constexpr (Traits::emotions) {
						u.add(u8_scale(c.mind.emotions.hatred, 100u), 3);
						u.add(u8_scale(c.mind.emotions.jealousy, 100u), 1);
					}
					u.add(foe->confidence, 2);
					u.add(proximity(*foe, px, py), 1);
					u.add(static_cast<eng::s32>(-bond), 2); // más odio, más empuje
					base = static_cast<Score>(u.score());
					eng::s16 mod = c.personality.aggression_mod();
					if constexpr (Traits::emotions) {
						mod = static_cast<eng::s16>(mod + c.mind.anger_mod() / 3);
					}
					base = apply_mod(base, mod);
				}
			}
			out[Behavior::Avenge] =
				weighted(base, weights.base[static_cast<eng::usize>(Behavior::Avenge)]);
		}

		// --- Someterse o resistir: evaluación jerárquica paramétrica ---
		{
			Score submit = 0;
			Score defy = 0;
			if (ctx.has_dominant) {
				submit = submission_score(ctx.self_power, ctx.dominant_power,
							  c.personality, c.mind);
				defy = defiance_score(ctx.self_power, ctx.dominant_power, c.personality,
						      c.mind);
			}
			out[Behavior::Submit] = weighted(
				submit, weights.base[static_cast<eng::usize>(Behavior::Submit)]);
			out[Behavior::Defy] =
				weighted(defy, weights.base[static_cast<eng::usize>(Behavior::Defy)]);
		}
	} else {
		out[Behavior::Tend] = 0;
		out[Behavior::Help] = 0;
		out[Behavior::Court] = 0;
		out[Behavior::Submit] = 0;
		out[Behavior::Defy] = 0;
		out[Behavior::Avenge] = 0;
	}

	// --- Enseñar: conocimiento disponible + diligencia (solo con conocimiento) ---
	if constexpr (Traits::knowledge) {
		eng::ai::Utility u;
		u.add(u8_scale(ctx.knowledge_confidence, 100u), 3);
		u.add(u8_scale(c.personality.diligence, 100u), 1);
		Score base = static_cast<Score>(u.score());
		base = apply_mod(base, c.personality.diligence_mod());
		out[Behavior::Teach] =
			weighted(base, weights.base[static_cast<eng::usize>(Behavior::Teach)]);
	} else {
		out[Behavior::Teach] = 0;
	}

	// --- Forrajear/trabajar para la colonia: diligencia + comida conocida ---
	if constexpr (Traits::society) {
		eng::ai::Utility u;
		u.add(u8_scale(c.personality.diligence, 100u), 3);
		u.add(pressure_score(c.needs.hunger), 1);
		Score base = static_cast<Score>(u.score());
		out[Behavior::Forage] =
			weighted(base, weights.base[static_cast<eng::usize>(Behavior::Forage)]);
	} else {
		out[Behavior::Forage] = 0;
	}
}

/// Elige el comportamiento activo aplicando ruido determinista y **histéresis**: se
/// cambia de comportamiento solo si el nuevo candidato supera al actual por `hysteresis`.
/// Devuelve el comportamiento elegido y deja el estado en `c.behavior`/`c.behavior_score`.
/// El ruido (`noise`, ±) sale de `rng`; con `noise == 0` la elección es determinista.
template <class Traits, class C, class Rng>
constexpr Behavior choose_behavior(C& c, const BehaviorContext& ctx, Rng& rng,
				   Score hysteresis = 120, eng::u32 noise = 40u,
				   const BehaviorWeights& weights = BehaviorWeights {}) noexcept {
	BehaviorScores scores {};
	score_behaviors<Traits>(c, ctx, scores, weights);

	// Ruido de decisión: evita que individuos idénticos actúen igual y que la criatura
	// quede atrapada en un empate permanente.
	if (noise != 0u) {
		for (eng::usize i = 0; i < behavior_count; ++i) {
			const eng::s32 noisy =
				static_cast<eng::s32>(scores.v[i]) + eng::next_symmetric(rng, noise);
			scores.v[i] = noisy < 0 ? static_cast<Score>(0) : static_cast<Score>(noisy);
		}
	}

	eng::usize best = 0u;
	for (eng::usize i = 1u; i < behavior_count; ++i) {
		if (scores.v[i] > scores.v[best]) {
			best = i;
		}
	}

	const eng::usize current = static_cast<eng::usize>(c.behavior);
	if (best == current) {
		c.behavior_score = scores.v[current];
		return c.behavior;
	}
	if (scores.v[best] >= static_cast<Score>(scores.v[current] + hysteresis)) {
		c.behavior = static_cast<Behavior>(best);
		c.behavior_score = scores.v[best];
		return c.behavior;
	}
	c.behavior_score = scores.v[current];
	return c.behavior;
}

/// Nombre legible de un comportamiento (diagnóstico/depuración; sin heap).
[[nodiscard]] constexpr const char* behavior_name(Behavior b) noexcept {
	switch (b) {
		case Behavior::Idle: return "idle";
		case Behavior::Wander: return "wander";
		case Behavior::Hunt: return "hunt";
		case Behavior::Flee: return "flee";
		case Behavior::SeekFood: return "seek_food";
		case Behavior::Sleep: return "sleep";
		case Behavior::Socialize: return "socialize";
		case Behavior::SeekShelter: return "seek_shelter";
		case Behavior::Tend: return "tend";
		case Behavior::Help: return "help";
		case Behavior::Court: return "court";
		case Behavior::Submit: return "submit";
		case Behavior::Defy: return "defy";
		case Behavior::Teach: return "teach";
		case Behavior::Forage: return "forage";
		case Behavior::Avenge: return "avenge";
		default: return "?";
	}
}

} // namespace eng::sim
