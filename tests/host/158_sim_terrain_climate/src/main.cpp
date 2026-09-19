// ============================================================================
// Test HOST-158: representacion del mundo (terreno) y clima global
// ============================================================================
//
// Valida:
//   - `eng/sim/terrain.hpp`: semantica de terreno (perfil, travesia por capacidades,
//     coste/cobertura/abrigo) y `TerrainMap<W,H>` para `eng::util::astar`.
//   - `eng/sim/climate.hpp`: clima por region (formar/disipar) y exposicion efectiva
//     segun el abrigo del terreno.
//   - Integracion en `SimWorld`: la exposicion se mitiga por la region y por estar a
//     cubierto; `region_passable` filtra por capacidades.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/158_sim_terrain_climate

#include <cstdio>

#include <eng/core/util/pathfinding.hpp>
#include <eng/sim/climate.hpp>
#include <eng/sim/terrain.hpp>
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

void test_terrain() {
	check(terrain_cost(TerrainKind::Floor) == 1u &&
		      terrain_cost(TerrainKind::Rough) == 3u,
	      "terreno: coste por tipo");
	check(terrain_cover(TerrainKind::Cover) > 0u &&
		      terrain_shelter(TerrainKind::Cover) > 0u,
	      "terreno: la cobertura protege");
	check(!can_traverse(movement::walk, TerrainKind::Wall), "terreno: el muro es infranqueable");
	check(!can_traverse(movement::walk, TerrainKind::Water) &&
		      can_traverse(movement::swim, TerrainKind::Water),
	      "terreno: el agua exige nadar");
	check(can_traverse(movement::fly, TerrainKind::Water) &&
		      can_traverse(movement::fly, TerrainKind::Gap),
	      "terreno: volar salva agua y huecos");
	check(can_traverse(movement::climb, TerrainKind::Ledge) &&
		      !can_traverse(movement::walk, TerrainKind::Ledge),
	      "terreno: la repisa exige escalar o saltar");
}

void test_terrain_map() {
	TerrainMap<4, 4> map;
	map.fill(TerrainKind::Floor);
	map.set(2u, 0u, TerrainKind::Wall);
	map.set(2u, 1u, TerrainKind::Wall);
	map.set(2u, 3u, TerrainKind::Wall);
	map.set(2u, 2u, TerrainKind::Cover);

	check(map.kind(2u, 0u) == TerrainKind::Wall &&
		      map.cost(TerrainMap<4, 4>::index(2u, 0u)) >= 255u,
	      "mapa: la celda recuerda su terreno");
	check(map.cover(TerrainMap<4, 4>::index(2u, 2u)) == terrain_cover(TerrainKind::Cover),
	      "mapa: cobertura por celda");

	// Integracion con eng::util::astar: rodear el muro por el hueco central de cobertura.
	eng::s16 came[16];
	eng::u16 gs[16];
	eng::u8 closed[16];
	const eng::u16 start = static_cast<eng::u16>(TerrainMap<4, 4>::index(0u, 2u));
	const eng::u16 goal = static_cast<eng::u16>(TerrainMap<4, 4>::index(3u, 2u));
	const bool found = eng::util::astar<4, 4>(
		start, goal,
		[&](eng::u16 idx) { return map.walkable(idx, movement::walk); },
		[&](eng::u16, eng::u16 to) { return map.cost(to); },
		eng::Span<eng::s16> {came, 16}, eng::Span<eng::u16> {gs, 16},
		eng::Span<eng::u8> {closed, 16});
	check(found, "mapa: astar encuentra el paso por la cobertura");

	// Objetivo encerrado: sin solucion.
	map.set(2u, 2u, TerrainKind::Wall);
	map.set(3u, 1u, TerrainKind::Wall);
	map.set(3u, 3u, TerrainKind::Wall);
	const bool trapped = eng::util::astar<4, 4>(
		start, goal,
		[&](eng::u16 idx) { return map.walkable(idx, movement::walk); },
		[&](eng::u16, eng::u16 to) { return map.cost(to); },
		eng::Span<eng::s16> {came, 16}, eng::Span<eng::u16> {gs, 16},
		eng::Span<eng::u8> {closed, 16});
	check(!trapped, "mapa: sin paso -> astar falla");
}

void test_climate() {
	Climate<16> cl;
	cl.set(2u, HazardKind::Cold, 200u);
	check(cl.severity(2u) == 200u && cl.at(2u).kind == HazardKind::Cold,
	      "clima: peligro por region");
	cl.add(2u, HazardKind::Cold, 100u);
	check(cl.severity(2u) == 255u, "clima: la severidad satura");
	cl.set(3u, HazardKind::Heat, 50u);
	check(cl.strongest() == 2u && cl.max_severity() == 255u, "clima: region mas severa");
	cl.tick(200u);
	check(cl.severity(2u) == 55u && cl.severity(3u) == 0u && cl.at(3u).kind == HazardKind::None,
	      "clima: el peligro se disipa");

	check(effective_exposure(200u, 0u) == 200u &&
		      effective_exposure(200u, 50u) == 100u &&
		      effective_exposure(200u, 100u) == 0u,
	      "clima: el abrigo mitiga la exposicion");

	RegionTerrain cover {TerrainKind::Cover, 30u, 0u};
	check(exposure_at(RegionHazard {HazardKind::Rain, 200u}, cover, false) == 20u,
	      "clima: abrigo de region + cobertura del terreno");
	check(exposure_at(RegionHazard {HazardKind::Rain, 200u}, cover, true) == 0u,
	      "clima: a cubierto no hay exposicion");
}

void test_world_integration() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.set_region(0u, RegionTerrain {TerrainKind::Cover, 30u, 0u});
	w.set_hazard(HazardKind::Rain, 200u);
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	w.find(a)->set_realized(true);
	eng::Xoroshiro64pp rng {1u, 2u};
	w.tick_realized(rng);
	check(w.find(a)->needs.exposure == 20u && w.find(a)->needs.hazard_kind() == HazardKind::Rain,
	      "mundo: la region mitiga la exposicion");

	// La lluvia solo en la region 1; la region 0 queda limpia.
	w.set_hazard(HazardKind::None, 0u);
	w.climate().set(1u, HazardKind::Storm, 220u);
	w.tick_realized(rng);
	check(w.find(a)->needs.exposure < 20u, "mundo: sin clima en su region, la exposicion cede");

	check(w.region_passable(0u, movement::walk) &&
		      !w.region_passable(0u, 0u),
	      "mundo: region_passable filtra por capacidades");
	w.set_terrain(0u, TerrainKind::Wall);
	check(!w.region_passable(0u, movement::walk), "mundo: un muro bloquea la region");
	check(w.terrain(0u) == TerrainKind::Wall, "mundo: terreno por region");
}

} // namespace

int main() {
	std::printf("Sim terrain/climate:\n");
	test_terrain();
	test_terrain_map();
	test_climate();
	test_world_integration();

	if (g_fail == 0u) {
		std::printf("OK: Sim terrain/climate (terreno, astar, clima, region)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
