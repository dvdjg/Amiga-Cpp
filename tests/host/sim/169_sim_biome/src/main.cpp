// ============================================================================
// Test HOST-169: biomas y ecosistemas (terreno + clima + especies por region)
// ============================================================================
//
// Valida `eng/sim/biome.hpp` y su volcado en el mundo:
//
//   1) `biome_profile`: cada bioma define terreno dominante, peligro climatico tipico,
//      abrigo, peligro y abundancia de comida.
//   2) `species_fits_biome`: una especie solo habita un bioma si puede moverse por su
//      terreno (nadar en la cienaga, trepar en la montana...).
//   3) `SimWorld::set_biome`/`apply_biome`: vuelcan el perfil en la region y, si se pide,
//      siembran su clima tipico.
//   4) `biome_species`/`biome_food`/`biome_name`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/169_sim_biome

#include <cstdio>

#include <eng/sim/biome.hpp>
#include <eng/sim/world.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_profiles() {
	check(biome_profile(BiomeKind::Plains).dominant == TerrainKind::Floor &&
		      biome_profile(BiomeKind::Swamp).dominant == TerrainKind::Water &&
		      biome_profile(BiomeKind::Mountain).dominant == TerrainKind::Climb,
	      "bioma: terreno dominante por bioma");
	check(biome_profile(BiomeKind::Desert).typical_hazard == HazardKind::Heat &&
		      biome_profile(BiomeKind::Tundra).typical_hazard == HazardKind::Cold &&
		      biome_profile(BiomeKind::Swamp).typical_hazard == HazardKind::Flood,
	      "bioma: clima tipico por bioma");
	check(biome_food(BiomeKind::Reef) > biome_food(BiomeKind::Desert),
	      "bioma: abundancia de comida");
	check(biome_name(BiomeKind::Forest)[0] == 'f', "bioma: nombre legible");
}

void test_species_fit() {
	Species walker {};
	walker.movement = movement::walk;
	Species swimmer {};
	swimmer.movement = movement::swim;
	Species climber {};
	climber.movement = movement::climb;

	check(species_fits_biome(walker, BiomeKind::Plains), "bioma: el caminante vive en la llanura");
	check(!species_fits_biome(walker, BiomeKind::Swamp) &&
		      species_fits_biome(swimmer, BiomeKind::Swamp),
	      "bioma: la cienaga exige nadar");
	check(species_fits_biome(climber, BiomeKind::Mountain) &&
		      !species_fits_biome(walker, BiomeKind::Mountain),
	      "bioma: la montana exige trepar");

	eng::u8 ids[4] {};
	check(biome_species(BiomeKind::Plains, eng::Span<eng::u8> {ids, 4}) == 0u,
	      "bioma: sin especies declaradas por defecto");
}

void test_world_biome() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.apply_biome(2u, BiomeKind::Tundra, true, 120u);
	check(w.biome(2u) == BiomeKind::Tundra && w.terrain(2u) == TerrainKind::Floor,
	      "world: el bioma fija el terreno");
	check(w.region(2u).shelter == biome_profile(BiomeKind::Tundra).shelter,
	      "world: el bioma fija abrigo/peligro");
	check(w.climate().severity(2u) == 120u &&
		      w.climate().at(2u).kind == HazardKind::Cold,
	      "world: el bioma siembra su clima tipico");

	w.set_biome(3u, BiomeKind::Swamp);
	check(w.terrain(3u) == TerrainKind::Water && w.region_passable(3u, movement::swim) &&
		      !w.region_passable(3u, movement::walk),
	      "world: el bioma cambia la travesia");
}

} // namespace

int main() {
	std::printf("Sim biome:\n");
	test_profiles();
	test_species_fit();
	test_world_biome();

	if (g_fail == 0u) {
		std::printf("OK: Sim biome (perfiles, especies, volcado al mundo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
