#pragma once

/// \file communication.hpp
/// **Lenguaje y gestos** (`eng::sim`): comunicación explícita entre criaturas, más allá de
/// los rumores. Una criatura **emite** una señal (voz/gesto/postura) que nace de su
/// conducta y su emoción, y las que la perciben actualizan su memoria de corto plazo y su
/// estado afectivo.
///
/// - `SignalKind`: llamada, alarma, amenaza, comida, cortejo, sumisión, saludo.
/// - `make_signal`: deriva la señal de la conducta/emoción; el alcance depende del oído y
///   de la ecolocalización.
/// - `receive_signals`: entrega a quien está en alcance (misma región), integrando el
///   tracker correspondiente (una alarma es un `Threat`, comida un `Item`, cortejo un
///   `Mate`...).
/// - `apply_signal_effect`: el contenido emocional de la señal (la alarma da miedo, la
///   sumisión eleva al que la recibe, el cortejo acerca...). Así el lenguaje se ata a
///   **emoción y jerarquía**.
///
/// Verificación: HOST-170.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/sim/behavior.hpp>
#include <eng/sim/mind.hpp>
#include <eng/sim/senses.hpp>
#include <eng/sim/tracker.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Tipo de señal (voz/gesto).
enum class SignalKind : eng::u8 {
	Call = 0,
	Alarm = 1,
	Threat = 2,
	Food = 3,
	Mating = 4,
	Submit = 5,
	Greet = 6,
	Count = 7,
};

/// Parámetros de emisión, alcance y efecto emocional de las señales.
struct SignalParams {
	eng::u8 base_range = 14u;      ///< alcance base
	eng::u8 intensity_min = 40u;   ///< por debajo no se emite (evita ruido)
	eng::u8 alarm_fear = 30u;      ///< miedo que infunde una alarma
	eng::u8 threat_anger = 20u;    ///< ira que despierta una amenaza
	eng::u8 mating_love = 25u;     ///< amor que despierta el cortejo
	eng::u8 submit_deference = 25u;///< deferencia que provoca la sumisión
	eng::u8 greet_cordiality = 25u;///< cordialidad que despierta un saludo
	eng::u8 food_joy = 20u;        ///< alegría del aviso de comida
};

/// Una señal emitida por una criatura.
struct Signal {
	EntityId sender = no_entity;
	FactionId faction = 0;
	RoomId room = no_room;
	eng::s16 x = 0;
	eng::s16 y = 0;
	SignalKind kind = SignalKind::Call;
	eng::u8 intensity = 0;
	eng::u8 range = 0;
};

/// Nombre legible (diagnóstico).
[[nodiscard]] constexpr const char* signal_name(SignalKind k) noexcept {
	switch (k) {
		case SignalKind::Call: return "call";
		case SignalKind::Alarm: return "alarm";
		case SignalKind::Threat: return "threat";
		case SignalKind::Food: return "food";
		case SignalKind::Mating: return "mating";
		case SignalKind::Submit: return "submit";
		case SignalKind::Greet: return "greet";
		default: return "?";
	}
}

/// El gesto que acompaña a una conducta y su emoción.
[[nodiscard]] constexpr SignalKind signal_for_behavior(Behavior b,
						       const Mind& m) noexcept {
	switch (b) {
		case Behavior::Flee: return SignalKind::Alarm;
		case Behavior::Hunt:
		case Behavior::Avenge: return SignalKind::Threat;
		case Behavior::SeekFood:
		case Behavior::Forage: return SignalKind::Food;
		case Behavior::Court: return SignalKind::Mating;
		case Behavior::Submit: return SignalKind::Submit;
		case Behavior::Socialize:
		case Behavior::Help:
		case Behavior::Tend: return SignalKind::Greet;
		default:
			// Sin conducta clara, la emoción decide: mucho miedo -> alarma.
			return m.emotions.fear > 180u ? SignalKind::Alarm : SignalKind::Call;
	}
}

/// Intensidad de la señal: urgencia de la conducta + emoción.
[[nodiscard]] constexpr eng::u8 signal_intensity(Behavior b, const Mind& m) noexcept {
	eng::u8 base = 80u;
	switch (b) {
		case Behavior::Flee: base = 220u; break;
		case Behavior::Avenge:
		case Behavior::Hunt: base = 180u; break;
		case Behavior::Court: base = 150u; break;
		case Behavior::Submit: base = 140u; break;
		case Behavior::SeekFood: base = 120u; break;
		default: base = 80u; break;
	}
	const eng::u8 emo = u8_max(m.emotions.fear, m.emotions.anger);
	return u8_sat_add(base, static_cast<eng::u8>(emo / 4u));
}

/// Construye la señal de una criatura; el alcance lo mejoran oído y ecolocalización.
[[nodiscard]] constexpr Signal make_signal(EntityId sender, FactionId faction, RoomId room,
					   eng::s16 x, eng::s16 y, Behavior behavior, const Mind& mind,
					   const Senses& senses,
					   const SignalParams& p = SignalParams {}) noexcept {
	Signal s {};
	s.sender = sender;
	s.faction = faction;
	s.room = room;
	s.x = x;
	s.y = y;
	s.kind = signal_for_behavior(behavior, mind);
	s.intensity = signal_intensity(behavior, mind);
	s.range = u8_sat_add(p.base_range,
			     static_cast<eng::u8>(senses.hearing / 8u + senses.echolocation / 4u));
	return s;
}

/// Tracker con el que se registra una señal recibida.
[[nodiscard]] constexpr TrackerKind tracker_for_signal(SignalKind k) noexcept {
	switch (k) {
		case SignalKind::Alarm:
		case SignalKind::Threat: return TrackerKind::Threat;
		case SignalKind::Food: return TrackerKind::Item;
		case SignalKind::Mating: return TrackerKind::Mate;
		case SignalKind::Submit:
		case SignalKind::Greet: return TrackerKind::Friend;
		default: return TrackerKind::Noise;
	}
}

/// Entrega las señales a un oyente: registra el tracker y devuelve cuántas oye.
template <eng::usize N>
constexpr eng::u8 receive_signals(TrackerList<N>& trackers, eng::Span<const Signal> signals,
				  RoomId room, eng::s16 x, eng::s16 y, eng::u16 tick,
				  const SignalParams& p = SignalParams {}) noexcept {
	(void)p;
	eng::u8 heard = 0u;
	for (eng::usize i = 0; i < signals.size(); ++i) {
		const Signal& s = signals[i];
		if (s.room != room || s.room == no_room) {
			continue;
		}
		const eng::u16 dist = manhattan(x, y, s.x, s.y);
		if (s.range == 0u || dist > s.range) {
			continue;
		}
		const eng::u8 strength = u8_scale(s.intensity, attenuation(dist, s.range));
		if (strength == 0u) {
			continue;
		}
		observe(trackers, tracker_for_signal(s.kind), s.sender, s.room, s.x, s.y, strength,
			tick);
		++heard;
	}
	return heard;
}

/// Efecto emocional de escuchar una señal (lenguaje ↔ emoción ↔ jerarquía).
constexpr void apply_signal_effect(Mind& m, SignalKind k,
				   const SignalParams& p = SignalParams {}) noexcept {
	switch (k) {
		case SignalKind::Alarm:
			m.emotions.fear = u8_sat_add(m.emotions.fear, p.alarm_fear);
			break;
		case SignalKind::Threat:
			m.emotions.fear = u8_sat_add(m.emotions.fear, p.threat_anger / 2u);
			m.emotions.anger = u8_sat_add(m.emotions.anger, p.threat_anger);
			break;
		case SignalKind::Mating:
			m.emotions.love = u8_sat_add(m.emotions.love, p.mating_love);
			break;
		case SignalKind::Submit:
			// Recibir una sumisión eleva al receptor (jerarquía) y lo sosiega.
			m.emotions.joy = u8_sat_add(m.emotions.joy, p.submit_deference / 2u);
			m.deference = u8_sat_sub(m.deference, p.submit_deference / 2u);
			break;
		case SignalKind::Greet:
			m.emotions.cordiality = u8_sat_add(m.emotions.cordiality, p.greet_cordiality);
			break;
		case SignalKind::Food:
			m.emotions.joy = u8_sat_add(m.emotions.joy, p.food_joy);
			break;
		default:
			break;
	}
}

} // namespace eng::sim
