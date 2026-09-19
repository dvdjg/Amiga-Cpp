#pragma once

/// \file knowledge.hpp
/// **Conocimiento** de una criatura (`eng::sim`): lo que ha **aprendido** sobre el mundo
/// y puede **compartir**. Frente a los `Tracker` (percepción inmediata, se olvida en
/// segundos), el conocimiento es memoria de largo plazo con confianza creciente: dónde
/// hay comida, qué especie es peligrosa, qué refugio sirve, cómo se usa una herramienta.
///
/// Reglas paramétricas (`LearningParams`), todas enteras:
/// - `learn`: refuerza una creencia existente o crea una nueva (si hay hueco; si no, cae
///   la de menor confianza). La confianza sube por `learn_gain` y se recorta a
///   `max_confidence`.
/// - `decay_knowledge`: la confianza baja por tick (olvido a largo plazo).
/// - `share`: **transmite** creencias de una criatura a otra (madre a cría, explorador a
///   manada, nodriza a colonia). El receptor aprende con la confianza del emisor menos
///   `share_loss`; así el conocimiento viaja y se refina.
///
/// El aprendizaje individual y su difusión son la base de la cultura de grupo; `diligence`
/// y `curiosity` influyen en el juego, no aquí, manteniendo este módulo puro.
///
/// Con `SimTraits::knowledge == false` el componente no se usa y el camino se elimina.
///
/// Verificación: HOST-154.

#include <eng/core/types.hpp>
#include <eng/core/util/static_vector.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Qué sabe una criatura.
enum class KnowledgeKind : eng::u8 {
	FoodSource = 0,  ///< dónde hay comida (sujeto = room/celda)
	WaterSource = 1, ///< dónde beber
	Shelter = 2,     ///< refugio válido (sujeto = room)
	Danger = 3,      ///< zona peligrosa (sujeto = room)
	Predator = 4,    ///< identidad de un depredador (sujeto = SpeciesId)
	Prey = 5,        ///< identidad de una presa
	Ally = 6,        ///< individuo/facción de confianza
	Enemy = 7,       ///< individuo/facción hostil
	ToolUse = 8,     ///< cómo usar/construir una herramienta (sujeto = objeto)
	Route = 9,       ///< camino entre regiones (sujeto = room destino)
	Ritual = 10,     ///< tradición/ritual conocido (sujeto = RitualKind)
	Count = 11,
};

/// Una creencia: tipo, sujeto genérico y confianza `[0, 255]`.
struct KnowledgeEntry {
	KnowledgeKind kind = KnowledgeKind::FoodSource;
	eng::u16 subject = 0;
	eng::u8 confidence = 0;
};

/// Conjunto acotado de creencias (capacidad `kMaxKnowledge`).
using KnowledgeSet = eng::util::StaticVector<KnowledgeEntry, kMaxKnowledge>;

/// Parámetros de aprendizaje y difusión.
struct LearningParams {
	eng::u8 learn_gain = 48u;     ///< cuánto sube la confianza por refuerzo
	eng::u8 decay = 2u;           ///< cuánto baja por tick sin reforzar
	eng::u8 share_loss = 24u;     ///< pérdida al transmitir una creencia
	eng::u8 teach_threshold = 160;///< confianza mínima para enseñar
	eng::u8 max_confidence = 255u;
};

/// Busca la creencia de `(kind, subject)` (o `nullptr`).
[[nodiscard]] constexpr KnowledgeEntry* find_knowledge(KnowledgeSet& set,
						       KnowledgeKind kind,
						       eng::u16 subject) noexcept {
	for (eng::usize i = 0; i < set.size(); ++i) {
		if (set[i].kind == kind && set[i].subject == subject) {
			return &set[i];
		}
	}
	return nullptr;
}

[[nodiscard]] constexpr const KnowledgeEntry* find_knowledge(const KnowledgeSet& set,
							     KnowledgeKind kind,
							     eng::u16 subject) noexcept {
	for (eng::usize i = 0; i < set.size(); ++i) {
		if (set[i].kind == kind && set[i].subject == subject) {
			return &set[i];
		}
	}
	return nullptr;
}

/// Aprende/refuerza una creencia. Si la lista está llena y no existe, sustituye la de
/// menor confianza (solo si la nueva aporta algo). Devuelve la confianza resultante.
constexpr eng::u8 learn(KnowledgeSet& set, KnowledgeKind kind, eng::u16 subject,
			eng::u8 gain = 48u, const LearningParams& p = LearningParams {}) noexcept {
	if (KnowledgeEntry* e = find_knowledge(set, kind, subject); e != nullptr) {
		const eng::u16 next = static_cast<eng::u16>(e->confidence) + gain;
		e->confidence = next > p.max_confidence ? p.max_confidence
						       : static_cast<eng::u8>(next);
		return e->confidence;
	}
	if (set.full()) {
		eng::usize weakest = 0u;
		for (eng::usize i = 1; i < set.size(); ++i) {
			if (set[i].confidence < set[weakest].confidence) {
				weakest = i;
			}
		}
		if (set[weakest].confidence >= gain) {
			return 0u; // lo aprendido no mejora lo que ya se sabe
		}
		set[weakest] = KnowledgeEntry {kind, subject, gain};
		return gain;
	}
	(void)set.push_back(KnowledgeEntry {kind, subject, gain});
	return gain;
}

/// Confianza sobre `(kind, subject)` (0 si no se sabe).
[[nodiscard]] constexpr eng::u8 confidence_for(const KnowledgeSet& set, KnowledgeKind kind,
					       eng::u16 subject) noexcept {
	const KnowledgeEntry* e = find_knowledge(set, kind, subject);
	return e != nullptr ? e->confidence : 0u;
}

/// ¿Se sabe con al menos `threshold` de confianza?
[[nodiscard]] constexpr bool knows(const KnowledgeSet& set, KnowledgeKind kind,
				   eng::u16 subject, eng::u8 threshold = 128u) noexcept {
	return confidence_for(set, kind, subject) >= threshold;
}

/// Olvido a largo plazo: baja la confianza y descarta lo olvidado.
constexpr void decay_knowledge(KnowledgeSet& set, eng::u8 amount) noexcept {
	for (eng::usize i = 0; i < set.size();) {
		KnowledgeEntry& e = set[i];
		e.confidence = u8_sat_sub(e.confidence, amount);
		if (e.confidence == 0u) {
			set.erase(i);
		} else {
			++i;
		}
	}
}

/// Creencia más fiable de un tipo (o `nullptr`).
[[nodiscard]] constexpr const KnowledgeEntry* best_knowledge(const KnowledgeSet& set,
							     KnowledgeKind kind) noexcept {
	const KnowledgeEntry* best = nullptr;
	for (eng::usize i = 0; i < set.size(); ++i) {
		if (set[i].kind != kind) {
			continue;
		}
		if (best == nullptr || set[i].confidence > best->confidence) {
			best = &set[i];
		}
	}
	return best;
}

/// Mayor confianza de todo el conjunto (para decidir si enseñar algo).
[[nodiscard]] constexpr eng::u8 top_confidence(const KnowledgeSet& set) noexcept {
	eng::u8 top = 0u;
	for (eng::usize i = 0; i < set.size(); ++i) {
		if (set[i].confidence > top) {
			top = set[i].confidence;
		}
	}
	return top;
}

/// Transmite conocimiento de `from` a `to` (el emisor enseña). Solo pasa creencias con
/// confianza `>= teach_threshold`; el receptor las recibe mermadas por `share_loss`.
/// Devuelve cuántas creencias se compartieron.
constexpr eng::u8 share(const KnowledgeSet& from, KnowledgeSet& to,
			const LearningParams& p = LearningParams {}) noexcept {
	eng::u8 shared = 0u;
	for (eng::usize i = 0; i < from.size(); ++i) {
		const KnowledgeEntry& e = from[i];
		if (e.confidence < p.teach_threshold) {
			continue;
		}
		const eng::u8 transferred = u8_sat_sub(e.confidence, p.share_loss);
		if (learn(to, e.kind, e.subject, transferred, p) > 0u) {
			++shared;
		}
	}
	return shared;
}

} // namespace eng::sim
