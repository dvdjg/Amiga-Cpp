#pragma once

/// \file colony.hpp
/// **Colonias de enjambre** (`eng::sim`): el comportamiento colectivo de hormigas,
/// abejas y termitas. Combina tres piezas:
///
/// - **Castas** (`genetics.hpp`): el genoma decide el rol (reina, obrera, soldado,
///   zángano, exploradora, nodriza); cada casta prefiere un comportamiento.
/// - **Censo y reparto de roles** (`Colony`): la colonia lleva la cuenta de sus castas y
///   pide la más necesaria según proporciones de diseño (`ColonyParams`).
/// - **Estigmergia (feromonas)**: la comunicación indirecta por el entorno reutiliza el
///   `eng::ai::InfluenceMap` de percepción (depositar, decaer, seguir el rastro más
///   fuerte). Así se reutiliza una primitiva ya probada en vez de duplicar una rejilla de
///   feromonas.
///
/// El resultado son rutas de forrajeo que se refuerzan solas, reclutamiento hacia la
/// comida y evitación de zonas marcadas como peligro, sin coordinador central.
///
/// ```text
///   genoma (castas)            Colony (censo + reparto de roles)      entorno (feromonas)
///   ───────────────            ────────────────────────────────       ───────────────────
///   reina / obrera / soldado ─► ColonyParams (proporciones) ─────► pide la casta más necesaria
///   zángano / exploradora /          │ note_birth / note_death            ▲
///   nodriza                          └─ census[Caste] → population()      │ depositar / decaer / seguir
///   estigmergia: reutiliza eng::ai::InfluenceMap ──────────────────────────┘ (sin coordinador central)
/// ```
///
/// Verificación: HOST-154.

#include <eng/ai/perception/influence_map.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/behavior.hpp>
#include <eng/sim/genetics.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Tipo de feromona (cada una es un mapa de influencia distinto).
enum class PheromoneKind : eng::u8 { Food = 0, Danger = 1, Home = 2, Recruit = 3, Count = 4 };

/// Parámetros de la colonia: proporciones de castas y dinámica de feromonas.
struct ColonyParams {
	eng::u8 worker_ratio = 50u;  ///< % objetivo de obreras
	eng::u8 soldier_ratio = 20u; ///< % objetivo de soldados
	eng::u8 scout_ratio = 15u;   ///< % objetivo de exploradoras
	eng::u8 nurse_ratio = 15u;   ///< % objetivo de nodrizas
	eng::u8 pheromone_deposit = 120u; ///< fuerza al depositar
	eng::u8 pheromone_decay = 3u;     ///< pérdida por tick
	eng::u8 recruit_min = 100u;       ///< influencia mínima para reclutar/seguir
};

/// Censo de castas de una colonia.
struct Colony {
	EntityId queen = no_entity;
	eng::u8 census[static_cast<eng::usize>(Caste::Count)] {};

	/// Carencia de una casta: `objetivo - actual` (puede ser negativa).
	[[nodiscard]] static constexpr eng::s16 gap(eng::u8 target, eng::u8 actual) noexcept {
		return static_cast<eng::s16>(static_cast<eng::s16>(target) -
					     static_cast<eng::s16>(actual));
	}

	/// Registra un nacimiento en la casta `c` (censos por casta, saturado a 255). Lo llaman
	/// los eventos de reproducción/migración.
	constexpr void note_birth(Caste c) noexcept {
		eng::u8& n = census[static_cast<eng::usize>(c)];
		if (n < 255u) {
			++n;
		}
	}
	/// Registra una muerte en la casta `c` (no baja de 0).
	constexpr void note_death(Caste c) noexcept {
		eng::u8& n = census[static_cast<eng::usize>(c)];
		if (n > 0u) {
			--n;
		}
	}
	/// Población total (suma de castas, saturada a 255); la consultan economía/IA.
	[[nodiscard]] constexpr eng::u8 population() const noexcept {
		eng::u16 sum = 0u;
		for (eng::usize i = 0; i < static_cast<eng::usize>(Caste::Count); ++i) {
			sum += census[i];
		}
		return sum > 255u ? static_cast<eng::u8>(255u) : static_cast<eng::u8>(sum);
	}

	/// Casta más necesaria según las proporciones objetivo (excluye reproductores). Es la
	/// señal que usaría una reina o una nodriza para decidir qué cría criar.
	[[nodiscard]] constexpr Caste needed_caste(const ColonyParams& p = ColonyParams {}) const noexcept {
		const eng::u8 total = population();
		if (total == 0u) {
			return Caste::Worker;
		}
		const eng::u8 target_worker = static_cast<eng::u8>(div_u16(
			static_cast<eng::u16>(static_cast<eng::u16>(total) * p.worker_ratio), 100u));
		const eng::u8 target_soldier = static_cast<eng::u8>(div_u16(
			static_cast<eng::u16>(static_cast<eng::u16>(total) * p.soldier_ratio), 100u));
		const eng::u8 target_scout = static_cast<eng::u8>(div_u16(
			static_cast<eng::u16>(static_cast<eng::u16>(total) * p.scout_ratio), 100u));
		const eng::u8 target_nurse = static_cast<eng::u8>(div_u16(
			static_cast<eng::u16>(static_cast<eng::u16>(total) * p.nurse_ratio), 100u));

		// Falta = objetivo - actual; se elige la mayor carencia.
		Caste best = Caste::Worker;
		eng::s16 best_gap = gap(target_worker, census[1]);
		const eng::s16 gaps[4] = {
			gap(target_worker, census[1]),   // Worker
			gap(target_soldier, census[2]),  // Soldier
			gap(target_scout, census[4]),    // Scout
			gap(target_nurse, census[5]),    // Nurse
		};
		const Caste castes[4] = {Caste::Worker, Caste::Soldier, Caste::Scout, Caste::Nurse};
		for (eng::usize i = 0; i < 4u; ++i) {
			if (gaps[i] > best_gap) {
				best_gap = gaps[i];
				best = castes[i];
			}
		}
		return best;
	}
};

/// Comportamiento que una casta prefiere (el juego lo usa como sesgo, no como imposición).
[[nodiscard]] constexpr Behavior caste_role_behavior(Caste c) noexcept {
	switch (c) {
		case Caste::Queen: return Behavior::Court;
		case Caste::Worker: return Behavior::Forage;
		case Caste::Soldier: return Behavior::Hunt;
		case Caste::Drone: return Behavior::Court;
		case Caste::Scout: return Behavior::Wander;
		case Caste::Nurse: return Behavior::Tend;
		default: return Behavior::Forage;
	}
}

// --- Estigmergia sobre una rejilla de influencia (reutiliza `eng::ai::InfluenceMap`) ---

/// Deposita feromona en `cell` (p. ej. al encontrar comida o peligro).
template <class Map>
constexpr void lay_pheromone(Map& map, eng::u16 cell,
			     const ColonyParams& p = ColonyParams {}) noexcept {
	map.deposit(cell, p.pheromone_deposit);
}

/// Refuerza una feromona existente en `cell` (la encontró de nuevo).
template <class Map>
constexpr void reinforce_pheromone(Map& map, eng::u16 cell, eng::u8 amount) noexcept {
	map.deposit(cell, amount);
}

/// Envejece la rejilla de feromonas (una vez por tick de colonia).
template <class Map>
constexpr void decay_pheromone(Map& map, const ColonyParams& p = ColonyParams {}) noexcept {
	map.decay(p.pheromone_decay);
}

/// Celda con el rastro más fuerte, si supera `recruit_min` (a dónde ir).
template <class Map>
[[nodiscard]] constexpr eng::u16 follow_pheromone(const Map& map,
						  const ColonyParams& p = ColonyParams {}) noexcept {
	const eng::u16 best = map.strongest();
	if (best == Map::no_cell || map.at(best) < p.recruit_min) {
		return Map::no_cell;
	}
	return best;
}

} // namespace eng::sim
