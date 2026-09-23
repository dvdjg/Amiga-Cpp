#pragma once

/// \file genetics.hpp
/// **Genética de enjambre** (`eng::sim`): dotación hereditaria de cada individuo y su
/// expresión en rasgos y castas. Modela de forma simplificada los mecanismos de hormigas,
/// abejas y termitas, pero sirve para cualquier especie con crías.
///
/// - `Genome`: ocho "genes" `u8` en `[0, 100]` (agresión, tamaño, velocidad,
///   sociabilidad, diligencia, empatía, dominancia, longevidad).
/// - `inherit`: combina dos genomas padre con **sesgo de dominancia** (qué progenitor
///   aporta cada gen) y aplica **mutación** determinista (`GeneticsParams`).
/// - `genome_to_personality`: **expresa** el genoma en la personalidad del individuo.
/// - `caste_of`: clasifica en una **casta** (reina/obrera/soldado/zángano/exploradora/
///   nodriza) con umbrales paramétricos (`CasteParams`). Cada casta tendrá un rol en la
///   colonia (`colony.hpp`).
///
/// Todo el azar es determinista y entero; cambiar los parámetros cambia la presión
/// evolutiva sin tocar el código.
///
/// Verificación: HOST-154.

#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/sim/personality.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Genes del genoma (índice en `Genome::genes`).
enum class Gene : eng::u8 {
	Aggression = 0,
	Size = 1,
	Speed = 2,
	Sociability = 3,
	Diligence = 4,
	Empathy = 5,
	Dominance = 6,
	Lifespan = 7,
	Count = 8,
};

inline constexpr eng::usize genome_size = static_cast<eng::usize>(Gene::Count);

/// Genoma: ocho genes `[0,100]` con 50 por defecto.
struct Genome {
	eng::u8 genes[genome_size] {50u, 50u, 50u, 50u, 50u, 50u, 50u, 50u};

	[[nodiscard]] constexpr eng::u8 gene(Gene g) const noexcept {
		return genes[static_cast<eng::usize>(g)];
	}
	constexpr void set(Gene g, eng::u8 value) noexcept {
		genes[static_cast<eng::usize>(g)] = value > 100u ? 100u : value;
	}

	/// Genoma aleatorio (para fundar una población).
	template <class Rng>
	[[nodiscard]] static constexpr Genome random(Rng& rng) noexcept {
		Genome g {};
		for (eng::usize i = 0; i < genome_size; ++i) {
			g.genes[i] = static_cast<eng::u8>(rng.next_mod(101u));
		}
		return g;
	}
};

/// Parámetros de recombinación y mutación.
struct GeneticsParams {
	eng::u8 dominance_bias = 50u; ///< probabilidad (%) de tomar cada gen del padre A
	eng::u8 mutation_rate = 25u;  ///< probabilidad (%) de mutar un gen
	eng::u8 mutation_span = 18u;  ///< amplitud de la mutación
};

/// Mutación de un gen (ruido simétrico, recortado a `[0,100]`).
template <class Rng>
[[nodiscard]] constexpr eng::u8 mutate_gene(eng::u8 gene, Rng& rng, eng::u8 span) noexcept {
	const eng::s32 v = static_cast<eng::s32>(gene) + eng::next_symmetric(rng, span);
	if (v < 0) {
		return 0u;
	}
	return v > 100 ? static_cast<eng::u8>(100u) : static_cast<eng::u8>(v);
}

/// Herencia: cada gen proviene de A o B según `dominance_bias` y puede mutar.
template <class Rng>
[[nodiscard]] constexpr Genome inherit(const Genome& a, const Genome& b, Rng& rng,
				       const GeneticsParams& p = GeneticsParams {}) noexcept {
	Genome child {};
	for (eng::usize i = 0; i < genome_size; ++i) {
		eng::u8 g = eng::chance(rng, p.dominance_bias, 100u) ? a.genes[i] : b.genes[i];
		if (eng::chance(rng, p.mutation_rate, 100u)) {
			g = mutate_gene(g, rng, p.mutation_span);
		}
		child.genes[i] = g;
	}
	return child;
}

/// Expresa el genoma en la personalidad del individuo.
[[nodiscard]] constexpr eng::u8 clamp100(eng::s32 v) noexcept {
	if (v < 0) {
		return 0u;
	}
	return v > 100 ? static_cast<eng::u8>(100u) : static_cast<eng::u8>(v);
}

/// Desplaza un gen respecto a 50 en `(gene-50)/div`, recortado a `[0,100]`.
[[nodiscard]] constexpr eng::u8 shift50(eng::u8 gene, eng::s32 div) noexcept {
	return clamp100(50 + (static_cast<eng::s32>(gene) - 50) / div);
}

[[nodiscard]] constexpr Personality genome_to_personality(const Genome& g) noexcept {
	Personality p {};
	p.aggression = g.gene(Gene::Aggression);
	p.bravery = shift50(g.gene(Gene::Lifespan), 4);
	p.nervousness = clamp100(100 - static_cast<eng::s32>(g.gene(Gene::Aggression)) / 2);
	p.sociability = g.gene(Gene::Sociability);
	p.dominance = g.gene(Gene::Dominance);
	p.curiosity = g.gene(Gene::Speed);
	p.empathy = g.gene(Gene::Empathy);
	p.loyalty = g.gene(Gene::Sociability);
	p.autonomy = clamp100(100 - static_cast<eng::s32>(g.gene(Gene::Sociability)) / 2);
	p.diligence = g.gene(Gene::Diligence);
	return p;
}

/// Casta dentro de una colonia de enjambre.
enum class Caste : eng::u8 {
	Queen = 0,
	Worker = 1,
	Soldier = 2,
	Drone = 3,
	Scout = 4,
	Nurse = 5,
	Count = 6,
};

/// Umbrales para clasificar la casta a partir del genoma.
struct CasteParams {
	eng::u8 queen_size = 85u;     ///< tamaño mínimo para ser reina
	eng::u8 soldier_aggression = 70u; ///< agresión mínima para soldado
	eng::u8 scout_speed = 70u;    ///< velocidad mínima para exploradora
	eng::u8 drone_size = 40u;     ///< tamaño máximo para zángano
	eng::u8 nurse_empathy = 70u;  ///< empatía mínima para nodriza
};

/// Clasifica la casta. Orden de prioridad: reina, soldado, exploradora, nodriza,
/// zángano, obrera.
[[nodiscard]] constexpr Caste caste_of(const Genome& g,
				       const CasteParams& p = CasteParams {}) noexcept {
	if (g.gene(Gene::Size) >= p.queen_size) {
		return Caste::Queen;
	}
	if (g.gene(Gene::Aggression) >= p.soldier_aggression) {
		return Caste::Soldier;
	}
	if (g.gene(Gene::Speed) >= p.scout_speed) {
		return Caste::Scout;
	}
	if (g.gene(Gene::Empathy) >= p.nurse_empathy) {
		return Caste::Nurse;
	}
	if (g.gene(Gene::Size) <= p.drone_size) {
		return Caste::Drone;
	}
	return Caste::Worker;
}

/// Nombre legible de una casta (diagnóstico; sin heap).
[[nodiscard]] constexpr const char* caste_name(Caste c) noexcept {
	switch (c) {
		case Caste::Queen: return "queen";
		case Caste::Worker: return "worker";
		case Caste::Soldier: return "soldier";
		case Caste::Drone: return "drone";
		case Caste::Scout: return "scout";
		case Caste::Nurse: return "nurse";
		default: return "?";
	}
}

/// ¿La casta trabaja para la colonia (forrajea/construye/cuida)?
[[nodiscard]] constexpr bool caste_works(Caste c) noexcept {
	return c == Caste::Worker || c == Caste::Scout || c == Caste::Nurse || c == Caste::Soldier;
}

/// ¿La casta se reproduce? (reina y zánganos).
[[nodiscard]] constexpr bool caste_reproduces(Caste c) noexcept {
	return c == Caste::Queen || c == Caste::Drone;
}

/// Sesga un genoma hacia una casta (lo usa una reina para orientar la puesta): ajusta
/// todos los genes relevantes para que `caste_of` devuelva esa casta de forma estable.
[[nodiscard]] constexpr Genome bias_for_caste(Genome g, Caste c,
					      const CasteParams& p = CasteParams {}) noexcept {
	const eng::u8 m = 5u;
	const eng::u8 low_soldier = u8_sat_sub(p.soldier_aggression, m);
	const eng::u8 low_scout = u8_sat_sub(p.scout_speed, m);
	const eng::u8 low_nurse = u8_sat_sub(p.nurse_empathy, m);
	const eng::u8 mid = u8_sat_add(p.drone_size, m); // tamano por encima de zangano
	switch (c) {
		case Caste::Queen:
			g.set(Gene::Size, u8_sat_add(p.queen_size, m));
			break;
		case Caste::Soldier:
			g.set(Gene::Size, mid);
			g.set(Gene::Aggression, u8_sat_add(p.soldier_aggression, m));
			break;
		case Caste::Scout:
			g.set(Gene::Size, mid);
			g.set(Gene::Aggression, low_soldier);
			g.set(Gene::Speed, u8_sat_add(p.scout_speed, m));
			break;
		case Caste::Nurse:
			g.set(Gene::Size, mid);
			g.set(Gene::Aggression, low_soldier);
			g.set(Gene::Speed, low_scout);
			g.set(Gene::Empathy, u8_sat_add(p.nurse_empathy, m));
			break;
		case Caste::Drone:
			g.set(Gene::Size, u8_sat_sub(p.drone_size, m));
			g.set(Gene::Aggression, low_soldier);
			g.set(Gene::Speed, low_scout);
			g.set(Gene::Empathy, low_nurse);
			break;
		default: // Worker: por debajo de todos los umbrales especiales
			g.set(Gene::Size, mid);
			g.set(Gene::Aggression, low_soldier);
			g.set(Gene::Speed, low_scout);
			g.set(Gene::Empathy, low_nurse);
			break;
	}
	return g;
}

} // namespace eng::sim
