#pragma once

/// \file domain.hpp
/// **Dominio GOAP de ejemplo** (`eng::sim`): objetos y construcción. Define hechos,
/// acciones y objetivos canónicos que el juego puede usar tal cual o como plantilla, y
/// que ejercitan `sim/planner.hpp` con un caso realista (conseguir comida, fabricar una
/// herramienta y construir un refugio).
///
/// Cadena de acciones:
///
/// ```
///   Forage ─► HasFood ─► Eat ─► Fed
///   Gather ─► HasMaterials ─► CraftTool ─► HasTool
///   HasTool + HasMaterials ─────────────► Build ─► Shelter
/// ```
///
/// El **inventario** (`SimInventory`) describe el estado del agente; `start_state` lo
/// traduce a hechos GOAP y `action_kind_of` mapea cada paso del plan a una acción
/// ejecutable (`SimActionKind`). El planificador real sigue siendo `eng::ai::Goap`.
///
/// Verificación: HOST-155 (y HOST-153 para la integración con el mundo).

#include <eng/ai/planning/goap.hpp>
#include <eng/ai/planning/htn.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/sim/planner.hpp>

namespace eng::sim {

/// Hechos del dominio de construcción (índices estables).
enum class SimFact : eng::u16 {
	HasFood = 0,
	Fed = 1,
	HasMaterials = 2,
	HasTool = 3,
	Shelter = 4,
	Count = 5,
};

/// Acciones ejecutables del dominio (coinciden con el índice del plan).
enum class SimActionKind : eng::u8 {
	Forage = 0,
	Eat = 1,
	Gather = 2,
	CraftTool = 3,
	Build = 4,
	Count = 5,
};

/// Estado del agente respecto al dominio.
struct SimInventory {
	bool has_food = false;
	bool has_materials = false;
	bool has_tool = false;
	bool fed = false;
	bool shelter_built = false;
};

/// Traduce el inventario a un estado GOAP de partida.
[[nodiscard]] constexpr SimGoap::State start_state(const SimInventory& inv) noexcept {
	SimGoap::State s {};
	if (inv.has_food) {
		s.facts.set(static_cast<eng::ai::Fact>(SimFact::HasFood));
	}
	if (inv.fed) {
		s.facts.set(static_cast<eng::ai::Fact>(SimFact::Fed));
	}
	if (inv.has_materials) {
		s.facts.set(static_cast<eng::ai::Fact>(SimFact::HasMaterials));
	}
	if (inv.has_tool) {
		s.facts.set(static_cast<eng::ai::Fact>(SimFact::HasTool));
	}
	if (inv.shelter_built) {
		s.facts.set(static_cast<eng::ai::Fact>(SimFact::Shelter));
	}
	return s;
}

/// Acción del dominio correspondiente al paso `index` de un plan.
[[nodiscard]] constexpr SimActionKind action_kind_of(eng::usize index) noexcept {
	return index < static_cast<eng::usize>(SimActionKind::Count)
		       ? static_cast<SimActionKind>(index)
		       : SimActionKind::Count;
}

/// Nombre legible de una acción (diagnóstico; sin heap).
[[nodiscard]] constexpr const char* action_name(SimActionKind a) noexcept {
	switch (a) {
		case SimActionKind::Forage: return "forage";
		case SimActionKind::Eat: return "eat";
		case SimActionKind::Gather: return "gather";
		case SimActionKind::CraftTool: return "craft_tool";
		case SimActionKind::Build: return "build_shelter";
		default: return "?";
	}
}

/// Dominio canónico de construcción: cinco acciones encadenables.
struct ConstructionDomain {
	using Ai = SimGoap;

	static constexpr eng::util::Array<Ai::Action, 5> actions() noexcept {
		return {{
			Ai::Builder {}.named("forage").produce(static_cast<eng::u16>(SimFact::HasFood)).build(),
			Ai::Builder {}
				.named("eat")
				.require(static_cast<eng::u16>(SimFact::HasFood))
				.consume(static_cast<eng::u16>(SimFact::HasFood))
				.produce(static_cast<eng::u16>(SimFact::Fed))
				.build(),
			Ai::Builder {}
				.named("gather")
				.produce(static_cast<eng::u16>(SimFact::HasMaterials))
				.build(),
			Ai::Builder {}
				.named("craft_tool")
				.require(static_cast<eng::u16>(SimFact::HasMaterials))
				.consume(static_cast<eng::u16>(SimFact::HasMaterials))
				.produce(static_cast<eng::u16>(SimFact::HasTool))
				.build(),
			Ai::Builder {}
				.named("build_shelter")
				.require(static_cast<eng::u16>(SimFact::HasTool),
					 static_cast<eng::u16>(SimFact::HasMaterials))
				.produce(static_cast<eng::u16>(SimFact::Shelter))
				.build(),
		}};
	}

	/// Objetivo: estar saciado y/o tener refugio.
	[[nodiscard]] static constexpr Ai::Goal goal(bool need_food,
						     bool need_shelter) noexcept {
		Ai::Goal g {};
		if (need_food) {
			g.want_true.facts.set(static_cast<eng::ai::Fact>(SimFact::Fed));
		}
		if (need_shelter) {
			g.want_true.facts.set(static_cast<eng::ai::Fact>(SimFact::Shelter));
		}
		return g;
	}
};

/// Red de tareas HTN del dominio de construcción (consumidor de `eng::ai::Htn`, G7.7). La
/// tarea compuesta 0 es **«conseguir refugio»**: con herramienta y materiales basta
/// construir; si no, se recoge, se fabrica, se recoge otra vez y se construye — el mismo
/// plan que obtiene el GOAP (HOST-318).
using SimHtn = eng::ai::Htn<32u, 5u, 1u, 4u, 8u>;

/// Construye la red de tareas de «conseguir refugio» (ver HOST-318).
[[nodiscard]] constexpr SimHtn build_shelter_htn() noexcept {
	SimHtn htn {};
	// Subtareas del método de respaldo: Gather, CraftTool, Gather, Build.
	htn.add_subtask(static_cast<eng::u16>(SimActionKind::Gather));
	htn.add_subtask(static_cast<eng::u16>(SimActionKind::CraftTool));
	htn.add_subtask(static_cast<eng::u16>(SimActionKind::Gather));
	htn.add_subtask(static_cast<eng::u16>(SimActionKind::Build));

	const SimHtn::Facts none {};
	SimHtn::Facts ready {};
	ready.set(static_cast<eng::ai::Fact>(SimFact::HasTool));
	ready.set(static_cast<eng::ai::Fact>(SimFact::HasMaterials));
	htn.add_method(ready, none, 3u, 1u); // método 0: ya hay herramientas -> solo construir
	htn.add_method(none, none, 0u, 4u);  // método 1 (respaldo): la cadena completa

	htn.add_compound(0u, 2u); // compuesta 0 = «conseguir refugio» con ambos métodos
	return htn;
}

} // namespace eng::sim
