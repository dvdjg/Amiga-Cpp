// ============================================================================
// Test HOST-157: objetos materiales y economia (inventario + mundо + reputacion)
// ============================================================================
//
// Valida:
//   - `eng/sim/inventory.hpp`: pilas de objetos (`Inventory`), etiquetas (`item_tags`) y
//     saturacion/capacidad.
//   - `eng/sim/object.hpp`: `ItemStore` (objetos del mundo) y `execute_domain_action`
//     (Forage/Eat/Gather/CraftTool/Build con efectos materiales).
//   - `eng/sim/economy.hpp`: precios por demanda/oferta y regalos/tributos que suben la
//     reputacion de una faccion (`Society`).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/157_sim_objects_economy

#include <cstdio>

#include <eng/sim/economy.hpp>
#include <eng/sim/inventory.hpp>
#include <eng/sim/object.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_inventory() {
	Inventory inv;
	check(inv.count(ItemKind::Food) == 0u, "inventario: arranca vacio");
	check(inv.add(ItemKind::Food, 3u) == 3u && inv.count(ItemKind::Food) == 3u,
	      "inventario: anade una pila");
	check(inv.has(ItemKind::Food, 3u) && !inv.has(ItemKind::Food, 4u),
	      "inventario: consulta de cantidad");

	// Saturacion de la pila a 255.
	inv.add(ItemKind::Food, 255u);
	check(inv.count(ItemKind::Food) == 255u, "inventario: la pila satura");
	check(inv.remove(ItemKind::Food, 200u) && inv.count(ItemKind::Food) == 55u,
	      "inventario: se quitan objetos");
	check(!inv.remove(ItemKind::Food, 100u), "inventario: no se quita lo que no hay");

	// Capacidad de pilas distintas.
	Inventory capped;
	capped.add(ItemKind::Food, 1u);
	capped.add(ItemKind::Material, 1u);
	capped.add(ItemKind::Tool, 1u);
	capped.add(ItemKind::Shelter, 1u);
	check(capped.add(ItemKind::Token, 1u) == 0u, "inventario: sin sitio para otra pila");

	check(has_tag(ItemKind::Food, item_tag::edible) &&
		      has_tag(ItemKind::Tool, item_tag::tool) &&
		      !has_tag(ItemKind::Food, item_tag::tool),
	      "inventario: etiquetas de objeto");
}

void test_objects() {
	Inventory inv;
	ItemStore store;
	check(execute_domain_action(inv, SimActionKind::Forage, &store, 0u, 0, 0) ==
		      ActionResult::Done &&
		      inv.count(ItemKind::Food) == 1u,
	      "objetos: Forage consigue comida");
	check(execute_domain_action(inv, SimActionKind::Eat, &store, 0u, 0, 0) ==
		      ActionResult::Done &&
		      inv.count(ItemKind::Food) == 0u,
	      "objetos: Eat consume comida");
	check(execute_domain_action(inv, SimActionKind::Eat, &store, 0u, 0, 0) ==
		      ActionResult::MissingResource,
	      "objetos: Eat sin comida falla");

	// Fabricar consume material y da herramienta.
	(void)execute_domain_action(inv, SimActionKind::Gather, &store, 0u, 0, 0);
	check(execute_domain_action(inv, SimActionKind::CraftTool, &store, 0u, 0, 0) ==
		      ActionResult::Done &&
		      inv.count(ItemKind::Tool) == 1u && inv.count(ItemKind::Material) == 0u,
	      "objetos: CraftTool consume material");

	// Construir necesita herramienta y material.
	(void)execute_domain_action(inv, SimActionKind::Gather, &store, 0u, 0, 0);
	check(execute_domain_action(inv, SimActionKind::Build, &store, 3u, 5, 6) ==
		      ActionResult::Done,
	      "objetos: Build construye con herramienta y material");
	check(store.count_kind(ItemKind::Shelter) == 1u, "objetos: el refugio queda en el mundo");
	const Item* shelter = store.find(1u);
	check(shelter != nullptr && shelter->kind == ItemKind::Shelter && shelter->room == 3u &&
		      shelter->x == 5 && shelter->y == 6,
	      "objetos: el refugio guarda posicion y tipo");

	// Sin herramienta, Build no puede.
	Inventory empty;
	check(execute_domain_action(empty, SimActionKind::Build, &store, 0u, 0, 0) ==
		      ActionResult::MissingResource,
	      "objetos: Build sin recursos falla");

	check(store.remove(1u) && store.count_kind(ItemKind::Shelter) == 0u,
	      "objetos: se retiran objetos del mundo");
}

void test_economy() {
	Economy eco;
	eco.reset();
	check(eco.value(ItemKind::Food) == 5 && eco.demand_of(ItemKind::Food) == 0u,
	      "economia: precio base");

	// La demanda sube el precio; la oferta lo baja.
	eco.register_demand(ItemKind::Food, 10u);
	check(eco.value(ItemKind::Food) == 15u, "economia: la demanda sube el precio");
	eco.register_supply(ItemKind::Food, 6u);
	check(eco.value(ItemKind::Food) == 9u, "economia: la oferta baja el precio");

	// El decaimiento devuelve demanda y precio hacia la base.
	for (int i = 0; i < 5; ++i) {
		eco.tick_decay();
	}
	check(eco.demand_of(ItemKind::Food) == 0u && eco.value(ItemKind::Food) == 5u,
	      "economia: la demanda decae");

	// Regalo: sube reputacion del receptor y la demanda del objeto.
	Society soc;
	Economy eco2;
	eco2.reset();
	const eng::s16 rep = give_gift(soc, eco2, 1u, ItemKind::Food, 10u);
	check(rep > 0 && soc.rep(1u) == rep, "economia: el regalo da reputacion");
	check(eco2.demand_of(ItemKind::Food) == 10u,
	      "economia: el regalo sube la demanda del objeto");

	// Tributo: pensado para dar mas reputacion.
	const eng::s16 tribute = offer_tribute(soc, eco2, 2u, ItemKind::Token, 4u);
	check(tribute > gift_reputation(eco2, ItemKind::Token, 4u, EconomyParams {}),
	      "economia: el tributo pesa mas que un regalo");
}

} // namespace

int main() {
	std::printf("Sim objects/economy:\n");
	test_inventory();
	test_objects();
	test_economy();

	if (g_fail == 0u) {
		std::printf("OK: Sim objects/economy (inventario, objetos, economia y reputacion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
