#pragma once

/// \file rumor.hpp
/// **Rumores y memoria de grupo** (`eng::sim`): el conocimiento que una facción acumula a
/// partir de lo que sus miembros han visto. Cierra el ciclo "lo que sé" → "lo que sabe mi
/// pueblo" → "cómo me trata el mundo".
///
/// - `GroupMemory<MaxFactions>`: un `KnowledgeSet` por facción; los individuos
///   **contribuyen** con sus creencias (con distorsión: un rumor pierde confianza) y la
///   facción **recuerda** la mejor versión.
/// - `contribute`: fusiona el conocimiento de una criatura con la memoria de su facción.
/// - `apply_group_knowledge`: traduce la memoria colectiva a efectos sociales y económicos
///   (una facción marcada como enemiga baja la reputación; saber de comida sube la
///   demanda). El sesgo lo fijan `RumorParams`.
///
/// No duplica `KnowledgeSet` (lo reutiliza) ni `Society`/`Economy` (los alimenta).
///
/// Verificación: HOST-159.

#include <eng/core/types.hpp>
#include <eng/sim/economy.hpp>
#include <eng/sim/inventory.hpp>
#include <eng/sim/knowledge.hpp>
#include <eng/sim/society.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Parámetros de difusión y efecto social de los rumores.
struct RumorParams {
	eng::u8 distortion = 24u;          ///< confianza que pierde una creencia al hacerse colectiva
	eng::u8 contribute_threshold = 120;///< confianza mínima para aportarla al grupo
	eng::u8 enemy_penalty = 30u;       ///< reputación que resta saber de un enemigo
	eng::u8 ally_bonus = 20u;          ///< reputación que suma saber de un aliado
	eng::u8 food_demand = 8u;          ///< demanda de comida que genera conocer una fuente
};

/// Memoria compartida por facción.
template <eng::u8 MaxFactions>
struct GroupMemory {
	KnowledgeSet factions[MaxFactions] {};

	[[nodiscard]] constexpr KnowledgeSet& of(FactionId f) noexcept {
		return factions[f < MaxFactions ? f : 0u];
	}
	[[nodiscard]] constexpr const KnowledgeSet& of(FactionId f) const noexcept {
		return factions[f < MaxFactions ? f : 0u];
	}
	[[nodiscard]] constexpr bool knows(FactionId f, KnowledgeKind kind, eng::u16 subject,
					   eng::u8 threshold = 128u) const noexcept {
		return eng::sim::knows(of(f), kind, subject, threshold);
	}
	constexpr void clear() noexcept {
		for (eng::u8 i = 0; i < MaxFactions; ++i) {
			factions[i].clear();
		}
	}
};

/// Aporta las creencias de una criatura a la memoria de su facción (con distorsión).
/// Devuelve cuántas creencias se incorporaron o reforzaron.
constexpr eng::u8 contribute(GroupMemory<kMaxFactions>& memory, FactionId faction,
			     const KnowledgeSet& set,
			     const RumorParams& rp = RumorParams {},
			     const LearningParams& lp = LearningParams {}) noexcept {
	eng::u8 n = 0u;
	KnowledgeSet& group = memory.of(faction);
	for (eng::usize i = 0; i < set.size(); ++i) {
		const KnowledgeEntry& e = set[i];
		if (e.confidence < rp.contribute_threshold) {
			continue;
		}
		const eng::u8 conf = u8_sat_sub(e.confidence, rp.distortion);
		if (learn(group, e.kind, e.subject, conf, lp) > 0u) {
			++n;
		}
	}
	return n;
}

/// Traduce la memoria colectiva de una facción a reputación (enemigos/aliados) y demanda
/// económica (búsqueda de comida). Devuelve el cambio neto de reputación.
constexpr eng::s16 apply_group_knowledge(Society& society, Economy& eco, FactionId faction,
					 const GroupMemory<kMaxFactions>& memory,
					 const RumorParams& rp = RumorParams {}) noexcept {
	const KnowledgeSet& group = memory.of(faction);
	eng::s16 rep_delta = 0;
	for (eng::usize i = 0; i < group.size(); ++i) {
		const KnowledgeEntry& e = group[i];
		if (e.confidence < 100u) {
			continue;
		}
		switch (e.kind) {
			case KnowledgeKind::Enemy: {
				if (e.subject < kMaxFactions) {
					society.adjust(static_cast<FactionId>(e.subject), -rp.enemy_penalty);
					rep_delta = static_cast<eng::s16>(rep_delta - rp.enemy_penalty);
				}
				break;
			}
			case KnowledgeKind::Ally: {
				if (e.subject < kMaxFactions) {
					society.adjust(static_cast<FactionId>(e.subject), rp.ally_bonus);
					rep_delta = static_cast<eng::s16>(rep_delta + rp.ally_bonus);
				}
				break;
			}
			case KnowledgeKind::FoodSource:
				eco.register_demand(ItemKind::Food, rp.food_demand);
				break;
			default:
				break;
		}
	}
	return rep_delta;
}

} // namespace eng::sim
