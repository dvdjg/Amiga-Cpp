#pragma once

/// \file biome.hpp
/// **Biomas y ecosistemas** (`eng::sim`): una capa que combina el clima, el terreno y las
/// especies por región. Un `BiomeKind` define el terreno dominante, el peligro climático
/// típico y la abundancia de comida; las especies "encajan" en un bioma si pueden moverse
/// por su terreno (`species_fits_biome`). `SimWorld::apply_biome` vuelca el perfil en la
/// región (terreno, abrigo, peligro) y, si se pide, siembra su clima típico: así una
/// ciénaga nace encharcada, una montaña exige trepar y un desierto trae calor.
///
/// Es la pieza que une el terreno dinámico (`terrain.hpp`), el clima (`climate.hpp`) y las
/// especies (`species.hpp`) como **una sola descripción de mundo**, sin duplicarlos.
///
/// Verificación: HOST-169.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/sim/climate.hpp>
#include <eng/sim/species.hpp>
#include <eng/sim/terrain.hpp>
#include <eng/sim/types.hpp>

namespace eng::sim {

/// Tipo de bioma.
enum class BiomeKind : eng::u8 {
	Plains = 0,
	Forest = 1,
	Swamp = 2,
	Desert = 3,
	Tundra = 4,
	Mountain = 5,
	Cave = 6,
	Reef = 7,
	Count = 8,
};

/// Número de especies típicas que declara un bioma.
inline constexpr eng::u8 kBiomeSpecies = 4u;

/// Descripción de un bioma: terreno, clima típico, recursos y especies.
struct BiomeProfile {
	TerrainKind dominant = TerrainKind::Floor;
	HazardKind typical_hazard = HazardKind::None;
	eng::u8 shelter = 0;    ///< abrigo extra de la región (0..100)
	eng::u8 danger = 0;     ///< peligro intrínseco (0..100)
	eng::u8 food = 50;      ///< abundancia de comida (0..100)
	SpeciesId species[kBiomeSpecies] {no_species, no_species, no_species, no_species};
};

/// Perfil de cada bioma.
[[nodiscard]] constexpr BiomeProfile biome_profile(BiomeKind b) noexcept {
	switch (b) {
		case BiomeKind::Plains:
			return {TerrainKind::Floor, HazardKind::None, 10u, 5u, 70u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Forest:
			return {TerrainKind::Cover, HazardKind::None, 60u, 20u, 80u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Swamp:
			return {TerrainKind::Water, HazardKind::Flood, 20u, 40u, 60u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Desert:
			return {TerrainKind::Rough, HazardKind::Heat, 0u, 30u, 15u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Tundra:
			return {TerrainKind::Floor, HazardKind::Cold, 20u, 25u, 20u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Mountain:
			return {TerrainKind::Climb, HazardKind::Storm, 40u, 45u, 25u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Cave:
			return {TerrainKind::Rough, HazardKind::None, 90u, 60u, 30u,
				{no_species, no_species, no_species, no_species}};
		case BiomeKind::Reef:
			return {TerrainKind::Water, HazardKind::Flood, 10u, 30u, 90u,
				{no_species, no_species, no_species, no_species}};
		default:
			return {};
	}
}

/// Nombre legible (diagnóstico; sin heap).
[[nodiscard]] constexpr const char* biome_name(BiomeKind b) noexcept {
	switch (b) {
		case BiomeKind::Plains: return "plains";
		case BiomeKind::Forest: return "forest";
		case BiomeKind::Swamp: return "swamp";
		case BiomeKind::Desert: return "desert";
		case BiomeKind::Tundra: return "tundra";
		case BiomeKind::Mountain: return "mountain";
		case BiomeKind::Cave: return "cave";
		case BiomeKind::Reef: return "reef";
		default: return "?";
	}
}

/// ¿La especie puede habitar el bioma? (puede moverse por su terreno dominante)
[[nodiscard]] constexpr bool species_fits_biome(const Species& s, BiomeKind b) noexcept {
	return can_traverse(s.movement, biome_profile(b).dominant);
}

/// Copia las especies típicas del bioma en `out` y devuelve cuántas hay.
[[nodiscard]] constexpr eng::u8 biome_species(BiomeKind b,
					      eng::Span<SpeciesId> out) noexcept {
	const BiomeProfile p = biome_profile(b);
	eng::u8 n = 0u;
	for (eng::u8 i = 0; i < kBiomeSpecies && n < out.size(); ++i) {
		if (p.species[i] != no_species) {
			out[n++] = p.species[i];
		}
	}
	return n;
}

/// Abundancia de comida del bioma (0..100).
[[nodiscard]] constexpr eng::u8 biome_food(BiomeKind b) noexcept {
	return biome_profile(b).food;
}

} // namespace eng::sim
