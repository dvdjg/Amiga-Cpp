// ============================================================================
// Test HOST-167: terreno dinamico y su interaccion con el clima
// ============================================================================
//
// Valida `apply_terrain_event` de `SimWorld` y los helpers de `terrain.hpp`:
//
//   1) Inundacion -> agua + clima de inundacion; cambia la travesia (nado).
//   2) Incendio -> zona peligrosa + calor. Derrumbe -> escombros + polvo.
//   3) Regeneracion -> cobertura y limpia el clima.
//   4) Parametros configurables y efecto solo en la region objetivo.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/167_sim_dynamic_terrain

#include <cstdio>

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

void test_events() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.link_rooms(0u, 1u);

	w.apply_terrain_event(0u, TerrainEvent::Flood);
	check(w.terrain(0u) == TerrainKind::Water, "terreno: la inundacion crea agua");
	check(w.climate().at(0u).kind == HazardKind::Flood && w.climate().severity(0u) == 100u,
	      "terreno: la inundacion trae clima de inundacion");
	check(!w.region_passable(0u, movement::walk) && w.region_passable(0u, movement::swim),
	      "terreno: cambia la travesia (nado)");
	check(w.terrain(1u) == TerrainKind::Floor, "terreno: solo cambia la region objetivo");

	w.apply_terrain_event(0u, TerrainEvent::Regrowth);
	check(w.terrain(0u) == TerrainKind::Cover && w.climate().severity(0u) == 0u,
	      "terreno: la regeneracion cubre y limpia el clima");

	w.apply_terrain_event(0u, TerrainEvent::Fire);
	check(w.terrain(0u) == TerrainKind::Hazard &&
		      w.climate().at(0u).kind == HazardKind::Heat,
	      "terreno: el incendio deja peligro y calor");

	w.apply_terrain_event(1u, TerrainEvent::Collapse);
	check(w.terrain(1u) == TerrainKind::Rough &&
		      w.climate().at(1u).kind == HazardKind::Dust,
	      "terreno: el derrumbe deja escombros y polvo");
}

void test_params() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	TerrainEventParams p {};
	p.flood_to = TerrainKind::Rough; // configuracion distinta
	p.flood_hazard = 40u;
	w.set_terrain_event_params(p);
	w.apply_terrain_event(0u, TerrainEvent::Flood);
	check(w.terrain(0u) == TerrainKind::Rough && w.climate().severity(0u) == 40u,
	      "terreno: los eventos son parametricos");

	// Region fuera de rango: no pasa nada.
	w.apply_terrain_event(200u, TerrainEvent::Flood);
	check(true, "terreno: region invalida se ignora sin romper");
}

void test_names() {
	check(terrain_name(TerrainKind::Water) != nullptr &&
		      terrain_event_name(TerrainEvent::Flood)[0] == 'f',
	      "terreno: nombres legibles");
	check(hazard_from_event(TerrainEvent::Regrowth) == HazardKind::None &&
		      hazard_from_event(TerrainEvent::Fire) == HazardKind::Heat,
	      "terreno: mapeo evento -> peligro");
}

} // namespace

int main() {
	std::printf("Sim dynamic terrain:\n");
	test_events();
	test_params();
	test_names();

	if (g_fail == 0u) {
		std::printf("OK: Sim dynamic terrain (inundacion, incendio, derrumbe, regeneracion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
