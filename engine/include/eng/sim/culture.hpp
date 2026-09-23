#pragma once

/// \file culture.hpp
/// **Cultura y rituales** (`eng::sim`): tradiciones que se **heredan por enseñanza** y se
/// **representan en grupo**. Un ritual es un conocimiento (`KnowledgeKind::Ritual` con
/// sujeto = `RitualKind`) que se transmite con `share` y que, al dispararse un evento
/// (muere un aliado, se encuentra comida, aparece un enemigo, cortejo...), se **actúa** con
/// una señal y un efecto emocional colectivo.
///
/// Así el lenguaje (`communication.hpp`) deja de ser reflejo individual y se vuelve
/// cultura: saludar al reencontrarse, guardar luto por un aliado, celebrar una caza. La
/// tradición se aprende, se comparte y se pierde si nadie la enseña.
///
/// Verificación: HOST-171.

#include <eng/core/types/types.hpp>
#include <eng/sim/communication.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Ritual/tradición.
enum class RitualKind : eng::u8 {
	Greeting = 0, ///< saludo al reencontrarse
	Mourning = 1, ///< luto por un aliado caído
	HuntCall = 2, ///< llamada de caza coordinada
	Feast = 3,    ///< celebración tras hallar comida
	Burial = 4,   ///< rito de despedida (calma al grupo)
	Count = 5,
};

/// Parámetros de los rituales (efecto emocional) y su disparo.
struct CultureParams {
	eng::u8 trigger_confidence = 128u; ///< confianza para considerar el ritual conocido
	eng::u8 greet_cordiality = 30u;
	eng::u8 mourn_relief = 35u;  ///< el luto consuela (baja tristeza)
	eng::u8 feast_joy = 40u;
	eng::u8 hunt_anger = 25u;
	eng::u8 burial_cordiality = 25u;
};

/// Nombre legible.
[[nodiscard]] constexpr const char* ritual_name(RitualKind r) noexcept {
	switch (r) {
		case RitualKind::Greeting: return "greeting";
		case RitualKind::Mourning: return "mourning";
		case RitualKind::HuntCall: return "hunt_call";
		case RitualKind::Feast: return "feast";
		case RitualKind::Burial: return "burial";
		default: return "?";
	}
}

/// Señal con la que se expresa un ritual.
[[nodiscard]] constexpr SignalKind signal_for_ritual(RitualKind r) noexcept {
	switch (r) {
		case RitualKind::Greeting: return SignalKind::Greet;
		case RitualKind::Mourning: return SignalKind::Call;
		case RitualKind::HuntCall: return SignalKind::Threat;
		case RitualKind::Feast: return SignalKind::Food;
		case RitualKind::Burial: return SignalKind::Submit;
		default: return SignalKind::Call;
	}
}

/// Aprende/refuerza un ritual (sujeto = `RitualKind`).
constexpr void learn_ritual(KnowledgeSet& kn, RitualKind r, eng::u8 gain = 128u) noexcept {
	(void)learn(kn, KnowledgeKind::Ritual, static_cast<eng::u16>(r), gain);
}

/// ¿Se conoce el ritual con suficiente confianza?
[[nodiscard]] constexpr bool knows_ritual(const KnowledgeSet& kn, RitualKind r,
					  eng::u8 threshold = 128u) noexcept {
	return knows(kn, KnowledgeKind::Ritual, static_cast<eng::u16>(r), threshold);
}

/// Confianza en un ritual.
[[nodiscard]] constexpr eng::u8 ritual_confidence(const KnowledgeSet& kn,
						  RitualKind r) noexcept {
	return confidence_for(kn, KnowledgeKind::Ritual, static_cast<eng::u16>(r));
}

/// Aplica el efecto emocional de actuar un ritual.
constexpr void perform_ritual(Mind& m, RitualKind r,
			      const CultureParams& p = CultureParams {}) noexcept {
	switch (r) {
		case RitualKind::Greeting:
			m.emotions.cordiality = u8_sat_add(m.emotions.cordiality, p.greet_cordiality);
			break;
		case RitualKind::Mourning:
			m.emotions.sadness = u8_sat_sub(m.emotions.sadness, p.mourn_relief);
			m.emotions.cordiality = u8_sat_add(m.emotions.cordiality, p.mourn_relief / 2u);
			break;
		case RitualKind::HuntCall:
			m.emotions.anger = u8_sat_add(m.emotions.anger, p.hunt_anger);
			m.emotions.fear = u8_sat_sub(m.emotions.fear, p.hunt_anger / 2u);
			break;
		case RitualKind::Feast:
			m.emotions.joy = u8_sat_add(m.emotions.joy, p.feast_joy);
			break;
		case RitualKind::Burial:
			m.emotions.sadness = u8_sat_sub(m.emotions.sadness, p.mourn_relief / 2u);
			m.emotions.cordiality = u8_sat_add(m.emotions.cordiality, p.burial_cordiality);
			break;
		default:
			break;
	}
}

/// Qué dispara un ritual.
enum class CultureEvent : eng::u8 {
	AllyDied = 0,
	FoodFound = 1,
	EnemyNear = 2,
	Mated = 3,
	Count = 4,
};

/// Ritual **conocido** que corresponde a un evento (o `Count` si no conoce ninguno).
[[nodiscard]] constexpr RitualKind ritual_for_event(
	const KnowledgeSet& kn, CultureEvent e,
	const CultureParams& p = CultureParams {}) noexcept {
	RitualKind candidates[2] = {RitualKind::Count, RitualKind::Count};
	switch (e) {
		case CultureEvent::AllyDied:
			candidates[0] = RitualKind::Burial;
			candidates[1] = RitualKind::Mourning;
			break;
		case CultureEvent::FoodFound:
			candidates[0] = RitualKind::Feast;
			break;
		case CultureEvent::EnemyNear:
			candidates[0] = RitualKind::HuntCall;
			break;
		case CultureEvent::Mated:
			candidates[0] = RitualKind::Greeting;
			break;
		default:
			break;
	}
	RitualKind best = RitualKind::Count;
	eng::u8 top = p.trigger_confidence;
	for (eng::u8 i = 0; i < 2u; ++i) {
		if (candidates[i] == RitualKind::Count) {
			continue;
		}
		const eng::u8 c = ritual_confidence(kn, candidates[i]);
		if (c >= top) {
			top = c;
			best = candidates[i];
		}
	}
	return best;
}

} // namespace eng::sim
