#pragma once

/// \file psyche_traits.hpp
/// **Rasgos de psique, aptitudes y defectos** de un personaje (`eng::sim`, capa de
/// persona). Completan los diez rasgos de `Personality` con lo que define a una persona
/// en la mesa: control de sí mismo, vida social, cognición, pericia adquirible y
/// taras/limitaciones que sesgan la conducta.
///
/// - `PsycheTraits`: veinte rasgos `u8` en `[0,100]` (50 = neutro), agrupados por función.
/// - `Skills`: quince **aptitudes** `u8` que se adquieren y se pierden (la pericia no es
///   carácter).
/// - `Flaws`: dieciséis **defectos** como bits de un `u32`; activan sesgos cuando
///   concurre su disparador (no son rasgos fijos).
///
/// Todo entero, sin heap y determinista; los modificadores usan `trait_mod`/`apply_mod`
/// (ya existentes en `eng/sim/types.hpp`), igual que `Personality`.
///
/// Verificación: HOST-199.

#include <eng/core/types/types.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Rasgos de psique, en `[0,100]` (50 = neutro). Complementan `Personality`.
struct PsycheTraits {
	// --- Control de sí mismo ---
	eng::u8 composure = 50u;     ///< autocontrol de la expresión y las emociones
	eng::u8 concentration = 50u; ///< atención sostenida a la mesa
	eng::u8 patience = 50u;      ///< tolerancia a esperar sin jugar
	eng::u8 temper = 50u;        ///< irascibilidad (alto = se enciende)
	eng::u8 impulsivity = 50u;   ///< actuar antes de pensar

	// --- Vida social ---
	eng::u8 deceit = 50u;        ///< habilidad y gusto por el engaño/farol
	eng::u8 gullibility = 50u;   ///< credulidad ante el engaño ajeno
	eng::u8 suspicion = 50u;     ///< tendencia a sospechar de todos
	eng::u8 honesty = 50u;       ///< aversión a mentir
	eng::u8 generosity = 50u;    ///< gusto por dar/compartir
	eng::u8 vanity = 50u;        ///< ego, necesidad de admiración
	eng::u8 morality = 50u;      ///< bondad frente a maldad
	eng::u8 humor = 50u;         ///< sentido del humor y ganas de broma
	eng::u8 talkativeness = 50u; ///< ganas de hablar/comentar
	eng::u8 self_esteem = 50u;   ///< valoración de sí mismo

	// --- Cognición y creencias ---
	eng::u8 attention = 50u;      ///< observación de los demás (detectar tells)
	eng::u8 social_memory = 50u;  ///< recordar el historial de cada rival
	eng::u8 superstition = 50u;   ///< atribuir suerte a amuletos/rituales
	eng::u8 risk = 50u;           ///< tolerancia al riesgo y a la varianza
	eng::u8 optimism = 50u;       ///< expectativa positiva del resultado

	static constexpr eng::u8 neutral = 50u;
};

/// Aptitudes adquiribles, en `[0,100]` (0 = no sabe, 100 = maestro). Suben con la
/// práctica y bajan con el desuso; no son carácter.
struct Skills {
	eng::u8 odds_math = 0u;           ///< calcular probabilidades y pot odds
	eng::u8 hand_selection = 0u;      ///< elegir con qué manos entrar
	eng::u8 bet_sizing = 0u;          ///< elegir tamaños de apuesta
	eng::u8 bluffing = 0u;            ///< ejecutar faroles creíbles
	eng::u8 trapping = 0u;            ///< tender trampas (slowplay)
	eng::u8 opponent_reading = 0u;    ///< leer tells y patrones del rival
	eng::u8 tell_control = 0u;        ///< ocultar las propias fugas
	eng::u8 deceit_detection = 0u;    ///< detectar que le mienten
	eng::u8 position_awareness = 0u;  ///< jugar la posición
	eng::u8 bankroll = 0u;            ///< gestión de la banca y del stack
	eng::u8 history_memory = 0u;      ///< recordar acciones concretas de rivales
	eng::u8 adaptation = 0u;          ///< cambiar de plan cuando el rival se adapta
	eng::u8 tilt_resistance = 0u;     ///< aguantar la mala racha sin descontrolarse
	eng::u8 table_presence = 0u;      ///< imponer respeto en la mesa
	eng::u8 endurance = 0u;           ///< rendir igual al final de una sesión larga
};

/// Defectos y limitaciones, como bits. Un defecto se **activa** cuando concurre su
/// disparador (racha, stack, fatiga); no es un rasgo permanente.
enum class Flaw : eng::u8 {
	Tilt = 0,             ///< pérdidas seguidas o mala jugada
	ChasingLosses = 1,    ///< va perdiendo
	Overconfidence = 2,   ///< racha ganadora
	FearOfLoss = 3,       ///< stack corto
	Greed = 4,            ///< bote grande
	Boredom = 5,          ///< muchos turnos sin jugar
	Distraction = 6,      ///< fatiga, entorno
	Drowsiness = 7,       ///< sesión larga
	Inebriation = 8,      ///< (opcional) estado alterado
	Paranoia = 9,         ///< derrotas
	Stubbornness = 10,    ///< discusión previa
	Denial = 11,          ///< error propio
	AnalysisParalysis = 12, ///< decisión difícil
	Copycat = 13,         ///< rival con éxito
	Predictability = 14,  ///< rutina
	Vengeance = 15,       ///< rival le ganó un bote
	Count = 16,
};

inline constexpr eng::usize flaw_count = static_cast<eng::usize>(Flaw::Count);

/// Conjunto de defectos de una persona (un bit por `Flaw`).
struct Flaws {
	eng::u32 bits = 0u;

	[[nodiscard]] constexpr bool has(Flaw f) const noexcept {
		return (bits & (1u << static_cast<eng::u8>(f))) != 0u;
	}
	constexpr void set(Flaw f) noexcept {
		bits |= (1u << static_cast<eng::u8>(f));
	}
	constexpr void clear(Flaw f) noexcept {
		bits &= ~(1u << static_cast<eng::u8>(f));
	}
	constexpr void toggle(Flaw f, bool on) noexcept {
		if (on) {
			set(f);
		} else {
			clear(f);
		}
	}
	[[nodiscard]] constexpr eng::u8 count() const noexcept {
		eng::u8 n = 0u;
		for (eng::u8 i = 0u; i < static_cast<eng::u8>(Flaw::Count); ++i) {
			if ((bits & (1u << i)) != 0u) {
				++n;
			}
		}
		return n;
	}
};

/// Nombre legible de un defecto (diagnóstico).
[[nodiscard]] constexpr const char* flaw_name(Flaw f) noexcept {
	switch (f) {
		case Flaw::Tilt: return "tilt";
		case Flaw::ChasingLosses: return "chasing_losses";
		case Flaw::Overconfidence: return "overconfidence";
		case Flaw::FearOfLoss: return "fear_of_loss";
		case Flaw::Greed: return "greed";
		case Flaw::Boredom: return "boredom";
		case Flaw::Distraction: return "distraction";
		case Flaw::Drowsiness: return "drowsiness";
		case Flaw::Inebriation: return "inebriation";
		case Flaw::Paranoia: return "paranoia";
		case Flaw::Stubbornness: return "stubbornness";
		case Flaw::Denial: return "denial";
		case Flaw::AnalysisParalysis: return "analysis_paralysis";
		case Flaw::Copycat: return "copycat";
		case Flaw::Predictability: return "predictability";
		case Flaw::Vengeance: return "vengeance";
		default: return "?";
	}
}

// --- Modificadores de rasgo de psique (mismo patrón que `Personality`) ---

[[nodiscard]] constexpr eng::s16 composure_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.composure);
}
[[nodiscard]] constexpr eng::s16 deceit_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.deceit);
}
[[nodiscard]] constexpr eng::s16 gullibility_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.gullibility);
}
[[nodiscard]] constexpr eng::s16 suspicion_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.suspicion);
}
[[nodiscard]] constexpr eng::s16 vanity_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.vanity);
}
[[nodiscard]] constexpr eng::s16 morality_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.morality);
}
[[nodiscard]] constexpr eng::s16 risk_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.risk);
}
[[nodiscard]] constexpr eng::s16 optimism_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.optimism);
}
[[nodiscard]] constexpr eng::s16 attention_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.attention);
}
[[nodiscard]] constexpr eng::s16 patience_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.patience);
}
[[nodiscard]] constexpr eng::s16 temper_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.temper);
}
[[nodiscard]] constexpr eng::s16 impulsivity_mod(const PsycheTraits& t) noexcept {
	return trait_mod(t.impulsivity);
}

} // namespace eng::sim
