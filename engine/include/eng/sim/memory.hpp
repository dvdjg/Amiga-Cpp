#pragma once

/// \file memory.hpp
/// **Memoria de dos niveles** (`eng::sim`): corto plazo (de trabajo) y largo plazo.
///
/// - **Corto plazo** (`TrackerList`): lo percibido hace poco, multimodal (los sentidos que
///   lo detectaron) y con `salience` (fuerza + novedad). Se olvida rápido (`decay`).
/// - **Largo plazo** (`KnowledgeSet`): creencias persistentes (semánticas y espaciales) que
///   sobreviven al olvido inmediato. La **consolidación** promueve a largo plazo lo que el
///   corto plazo sostiene con fuerza o durante suficiente tiempo (la recurrencia importa,
///   como en la memoria biológica), y el conocimiento se comparte después (`share`).
///
/// Esta separación deja el bucle por frame barato: la decisión mira el corto plazo; el
/// largo plazo se actualiza cada `consolidation_period` ticks. Reutiliza `Tracker` y
/// `KnowledgeSet`; no duplica ninguno.
///
/// Verificación: HOST-161.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/senses.hpp>
#include <eng/sim/tracker.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Parámetros de atención, olvido y consolidación.
struct MemoryParams {
	eng::u8 consolidation_threshold = 170u; ///< fuerza/confianza mínima para consolidar
	eng::u8 consolidation_gain = 48u;       ///< confianza que gana la creencia al consolidar
	eng::u8 forget_rate = 1u;               ///< olvido del corto plazo por tick
	eng::u8 recurring_bonus = 40u;          ///< bonus de saliencia si ya se sostenía
	eng::u8 place_decay = 3u;               ///< olvido de lugares (mapa mental) por ciclo
	eng::u16 consolidation_period = 60u;    ///< ticks entre consolidaciones
};

/// Integra observaciones en la memoria de corto plazo (trackers): refresca posición y
/// confianza, **acumula las modalidades** que lo detectaron y conserva la mayor saliencia.
template <eng::usize N>
constexpr void integrate_observations(TrackerList<N>& trackers,
				      eng::Span<const Observation> observations,
				      eng::u16 tick) noexcept {
	for (eng::usize i = 0; i < observations.size(); ++i) {
		const Observation& o = observations[i];
		observe(trackers, o.kind, o.target, o.room, o.x, o.y, o.strength, tick);
		if (Tracker* t = find_tracker(trackers, o.target, o.kind); t != nullptr) {
			t->modalities = static_cast<eng::u8>(t->modalities | o.modalities);
			if (o.salience > t->salience) {
				t->salience = o.salience;
			} else {
				// La recurrencia refuerza la atención aunque la fuerza baje.
				t->salience = u8_sat_add(t->salience, 8u);
			}
		}
	}
}

/// Fuerza en corto plazo de un objetivo (máxima confianza entre sus trackers).
template <eng::usize N>
[[nodiscard]] constexpr eng::u8 working_strength(const TrackerList<N>& trackers,
						 EntityId target) noexcept {
	eng::u8 best = 0u;
	for (eng::usize i = 0; i < trackers.size(); ++i) {
		if (trackers[i].target == target && trackers[i].confidence > best) {
			best = trackers[i].confidence;
		}
	}
	return best;
}

/// Olvido del corto plazo.
template <eng::usize N>
constexpr void forget_working(TrackerList<N>& trackers, const MemoryParams& p) noexcept {
	decay(trackers, p.forget_rate);
}

/// Traduce la categoría de un tracker a la de una creencia de largo plazo.
[[nodiscard]] constexpr KnowledgeKind knowledge_of(TrackerKind k) noexcept {
	switch (k) {
		case TrackerKind::Prey: return KnowledgeKind::Prey;
		case TrackerKind::Threat: return KnowledgeKind::Enemy;
		case TrackerKind::Rival: return KnowledgeKind::Enemy;
		case TrackerKind::Friend: return KnowledgeKind::Ally;
		case TrackerKind::Kin: return KnowledgeKind::Ally;
		case TrackerKind::Mate: return KnowledgeKind::Ally;
		case TrackerKind::Item: return KnowledgeKind::FoodSource;
		case TrackerKind::Den: return KnowledgeKind::Shelter;
		case TrackerKind::Injury: return KnowledgeKind::Danger;
		case TrackerKind::Noise: return KnowledgeKind::Danger;
		default: return KnowledgeKind::Danger;
	}
}

/// ¿La creencia es espacial (su sujeto es una región)? Den e Item apuntan al lugar.
[[nodiscard]] constexpr bool knowledge_is_place(TrackerKind k) noexcept {
	return k == TrackerKind::Den || k == TrackerKind::Item || k == TrackerKind::Injury ||
	       k == TrackerKind::Noise;
}

/// **Consolida** en largo plazo los trackers suficientemente fuertes o salientes.
/// Devuelve cuántas creencias se crearon o reforzaron.
template <eng::usize N>
constexpr eng::u8 consolidate(const TrackerList<N>& trackers, KnowledgeSet& knowledge,
			      const MemoryParams& p = MemoryParams {}) noexcept {
	eng::u8 n = 0u;
	for (eng::usize i = 0; i < trackers.size(); ++i) {
		const Tracker& t = trackers[i];
		if (t.confidence < p.consolidation_threshold &&
		    t.salience < p.consolidation_threshold) {
			continue;
		}
		const KnowledgeKind kk = knowledge_of(t.kind);
		const eng::u16 subject = knowledge_is_place(t.kind) && t.room != no_room
						 ? t.room
						 : t.target;
		const eng::u8 gain = u8_sat_add(p.consolidation_gain,
						static_cast<eng::u8>(t.confidence / 2u));
		if (learn(knowledge, kk, subject, gain) > 0u) {
			++n;
		}
	}
	return n;
}

/// Confianza de una creencia de largo plazo (0 si no se recuerda).
[[nodiscard]] constexpr eng::u8 recall(const KnowledgeSet& knowledge, KnowledgeKind kind,
				       eng::u16 subject) noexcept {
	return confidence_for(knowledge, kind, subject);
}

/// ¿Recuerda que una región es un refugio?
[[nodiscard]] constexpr bool recalls_shelter(const KnowledgeSet& knowledge,
					     RoomId room) noexcept {
	return knows(knowledge, KnowledgeKind::Shelter, room, 128u);
}

/// ¿Recuerda que una región es peligrosa?
[[nodiscard]] constexpr bool recalls_danger(const KnowledgeSet& knowledge,
					    RoomId room) noexcept {
	return knows(knowledge, KnowledgeKind::Danger, room, 128u);
}

/// **Memoria espacial**: refuerza el recuerdo de un lugar (región) como refugio, fuente de
/// comida o peligro. Es la base del mapa mental que usa el pathfinding macro.
constexpr void remember_place(KnowledgeSet& knowledge, KnowledgeKind kind, RoomId room,
			      eng::u8 gain = 128u) noexcept {
	if (room == no_room) {
		return;
	}
	(void)learn(knowledge, kind, room, gain);
}

/// Confianza de un lugar para un tipo de creencia.
[[nodiscard]] constexpr eng::u8 place_confidence(const KnowledgeSet& knowledge,
						 KnowledgeKind kind, RoomId room) noexcept {
	return confidence_for(knowledge, kind, room);
}

/// Lugar recordado (región) con más confianza para un tipo, si supera `threshold`.
[[nodiscard]] constexpr RoomId best_known_room(const KnowledgeSet& knowledge,
					       KnowledgeKind kind,
					       eng::u8 threshold = 128u) noexcept {
	RoomId best = no_room;
	eng::u8 top = threshold;
	for (eng::usize i = 0; i < knowledge.size(); ++i) {
		const KnowledgeEntry& e = knowledge[i];
		if (e.kind != kind || e.subject >= 255u) {
			continue;
		}
		if (e.confidence >= top) {
			top = e.confidence;
			best = static_cast<RoomId>(e.subject);
		}
	}
	return best;
}

/// ¿El tipo de creencia es espacial (lugar/región)?
[[nodiscard]] constexpr bool is_place_kind(KnowledgeKind kind) noexcept {
	switch (kind) {
		case KnowledgeKind::FoodSource:
		case KnowledgeKind::WaterSource:
		case KnowledgeKind::Shelter:
		case KnowledgeKind::Danger:
		case KnowledgeKind::Route:
			return true;
		default:
			return false;
	}
}

/// **Olvido de lugares**: decae solo el mapa mental (no las creencias sobre individuos) y
/// descarta los lugares olvidados. Cierra el ciclo "el mapa se desactualiza".
constexpr void decay_places(KnowledgeSet& knowledge, eng::u8 amount) noexcept {
	for (eng::usize i = 0; i < knowledge.size();) {
		KnowledgeEntry& e = knowledge[i];
		if (!is_place_kind(e.kind)) {
			++i;
			continue;
		}
		e.confidence = u8_sat_sub(e.confidence, amount);
		if (e.confidence == 0u) {
			knowledge.erase(i);
		} else {
			++i;
		}
	}
}

/// Olvida explícitamente un lugar.
constexpr void forget_place(KnowledgeSet& knowledge, KnowledgeKind kind,
			    RoomId room) noexcept {
	for (eng::usize i = 0; i < knowledge.size(); ++i) {
		if (knowledge[i].kind == kind && knowledge[i].subject == room) {
			knowledge.erase(i);
			return;
		}
	}
}

} // namespace eng::sim
