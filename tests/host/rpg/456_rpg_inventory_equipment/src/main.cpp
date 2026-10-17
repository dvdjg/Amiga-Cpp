// ============================================================================
// Test HOST-456: inventario, equipo y atributos derivados (`eng::rpg`)
// ============================================================================
//
// Valida `engine/include/eng/rpg/{inventory,equipment,stats}.hpp`:
//
//   1) `ItemBag` + `bag_*`: entradas con capacidad, apilado sobre pilas compatibles,
//      resto que no entra, consultas por tipo/etiqueta y peso total.
//   2) `Equipment` + `equip_item`/`unequip_item`: la etiqueta exigida por ranura es dato;
//      sustitucion con devolucion de la pieza anterior y rechazo de handles caducados.
//   3) `gather_equipment_mods` + `recompute_stats`: pipeline de atributos por fases fijas
//      (suma, porcentaje acumulado, recorte a s16) con aritmetica nativa del 68000.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/456_rpg_inventory_equipment

#include <cstdio>

#include <eng/rpg/equipment.hpp>
#include <eng/rpg/inventory.hpp>
#include <eng/rpg/stats.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

/// Atributos de ejemplo: 0 = ataque, 1 = defensa (el juego define los suyos).
enum : eng::u8 { kAtk = 0u, kDef = 1u };

struct Fixture {
	ItemCatalog<4> catalog {};
	ItemStore<8> store {};
	eng::util::Array<StatMod, 2> def_mods {}; // tabla de modificadores del contenido
	ItemTypeId sword = no_item_type;
	ItemTypeId armor = no_item_type;
	ItemTypeId potion = no_item_type;
	ItemTypeId knife = no_item_type;

	Fixture() {
		ItemDef sword_def {};
		sword_def.tags = item_tags::weapon;
		sword_def.weight = 3u;
		sword_def.mod_first = 0u;
		sword_def.mod_count = 1u;
		sword = catalog.add(sword_def);

		ItemDef armor_def {};
		armor_def.tags = item_tags::armor;
		armor_def.weight = 8u;
		armor_def.mod_first = 1u;
		armor_def.mod_count = 1u;
		armor = catalog.add(armor_def);

		ItemDef potion_def {};
		potion_def.tags = item_tags::food | item_tags::drink;
		potion_def.weight = 1u;
		potion_def.stack_max = 5u;
		potion = catalog.add(potion_def);

		ItemDef knife_def {};
		knife_def.tags = item_tags::weapon | item_tags::tool;
		knife_def.weight = 1u;
		knife = catalog.add(knife_def);

		// Modificadores del catalogo: espada +3 ataque, armadura +5 defensa.
		def_mods[0] = StatMod {kAtk, 3, 0};
		def_mods[1] = StatMod {kDef, 5, 0};
	}

	ItemHandle spawn(ItemTypeId type, eng::u8 qty = 1u) {
		ItemInstance inst {};
		inst.type = type;
		inst.qty = qty;
		return store.spawn(inst);
	}
};

void test_bag() {
	Fixture fx;
	ItemBag<3> bag {};

	const ItemHandle potion_a = fx.spawn(fx.potion, 3u);
	const ItemHandle potion_b = fx.spawn(fx.potion, 4u);
	const ItemHandle sword = fx.spawn(fx.sword);
	const ItemHandle knife = fx.spawn(fx.knife);

	check(bag_add(bag, fx.store, fx.catalog, potion_a) == 0u, "bolsa: pila nueva entra");
	check(bag.size() == 1u && bag.contains(potion_a), "bolsa: entrada presente");

	// La segunda pocima apila 2 sobre la primera (hasta 5) y deja 2 en una pila nueva.
	check(bag_add(bag, fx.store, fx.catalog, potion_b) == 0u, "bolsa: apilado completo");
	check(fx.store.get(potion_a)->qty == 5u, "bolsa: pila llena con lo apilado");
	check(fx.store.get(potion_b)->qty == 2u, "bolsa: resto en su instancia");
	check(bag.size() == 2u && bag.contains(potion_b), "bolsa: segunda pila creada");
	check(bag_count(bag, fx.store, fx.potion) == 7u, "bolsa: unidades totales del tipo");

	check(bag_add(bag, fx.store, fx.catalog, sword) == 0u, "bolsa: tercera entrada");
	check(bag.full(), "bolsa: llena");

	// Sin hueco ni pila compatible, el cuchillo no entra: el resto se devuelve.
	check(bag_add(bag, fx.store, fx.catalog, knife) == 1u, "bolsa: resto 1 al estar llena");
	check(!bag.contains(knife), "bolsa: rechazado no queda dentro");

	check(bag_find_type(bag, fx.store, fx.potion) == potion_a, "bolsa: find por tipo");
	check(!bag_find_type(bag, fx.store, fx.knife).valid(), "bolsa: find sin resultado");
	check(bag_find_tag(bag, fx.store, fx.catalog, item_tags::weapon) == sword,
	      "bolsa: find por etiqueta arma");
	check(!bag_find_tag(bag, fx.store, fx.catalog, item_tags::key).valid(),
	      "bolsa: find por etiqueta ausente");

	// Peso: pocimas 5x1 + 2x1, espada 3x1.
	check(bag_total_weight(bag, fx.store, fx.catalog) == 10u, "bolsa: peso total");

	check(bag.remove(sword), "bolsa: quitar entrada");
	check(!bag.remove(sword), "bolsa: quitar dos veces falla");
	check(bag.size() == 2u && !bag.full(), "bolsa: hueco libre tras quitar");
	check(bag.put(sword), "bolsa: put directo sin apilar");
	check(bag.size() == 3u, "bolsa: put ocupa el hueco");
}

void test_equipment() {
	Fixture fx;
	Equipment<2> eq {};
	eq.required_tags[0] = item_tags::weapon;
	eq.required_tags[1] = item_tags::armor;

	const ItemHandle sword = fx.spawn(fx.sword);
	const ItemHandle armor = fx.spawn(fx.armor);
	const ItemHandle potion = fx.spawn(fx.potion);
	const ItemHandle knife = fx.spawn(fx.knife);

	ItemHandle replaced {};
	check(equip_item(eq, fx.store, fx.catalog, 0u, sword, replaced), "equipo: espada entra");
	check(!replaced.valid() && eq.slots[0] == sword, "equipo: ranura ocupada sin sustituta");

	check(!equip_item(eq, fx.store, fx.catalog, 0u, potion, replaced),
	      "equipo: la pocima no cumple la etiqueta de arma");
	check(!replaced.valid() && eq.slots[0] == sword, "equipo: rechazo no toca la ranura");

	check(equip_item(eq, fx.store, fx.catalog, 0u, knife, replaced), "equipo: cuchillo entra");
	check(replaced == sword && eq.slots[0] == knife, "equipo: sustituta devuelta");

	check(equip_item(eq, fx.store, fx.catalog, 1u, armor, replaced), "equipo: armadura entra");

	check(!equip_item(eq, fx.store, fx.catalog, 2u, knife, replaced),
	      "equipo: ranura fuera de rango rechazada");

	const ItemHandle taken = unequip_item(eq, 0u);
	check(taken == knife && !eq.slots[0].valid(), "equipo: desequipar devuelve la pieza");
	const ItemHandle taken_armor = unequip_item(eq, 1u);
	check(taken_armor == armor && !eq.slots[1].valid(), "equipo: desequipar la armadura");
	check(!unequip_item(eq, 3u).valid(), "equipo: desequipar ranura invalida");

	// Vuelta a colocar para los mods.
	(void)equip_item(eq, fx.store, fx.catalog, 0u, sword, replaced);
	(void)equip_item(eq, fx.store, fx.catalog, 1u, armor, replaced);

	eng::util::Array<StatMod, 4> collected {};
	ModSources tables {};
	tables.def_mods = fx.def_mods.span();
	const eng::u16 written =
	    gather_equipment_mods(eq, fx.store, fx.catalog, tables, collected.span());
	check(written == 2u, "equipo: dos modificadores recolectados");
	check(collected[0].stat == kAtk && collected[0].add == 3, "equipo: +3 ataque de la espada");
	check(collected[1].stat == kDef && collected[1].add == 5, "equipo: +5 defensa de la armadura");

	// Handle caducado: destruir una espada y tratar de equiparla (la etiqueta cumpliria,
	// pero la instancia ya no existe: el rechazo es por el handle, no por la ranura).
	const ItemHandle ghost = fx.spawn(fx.sword);
	(void)fx.store.destroy(ghost);
	check(!equip_item(eq, fx.store, fx.catalog, 0u, ghost, replaced),
	      "equipo: handle caducado rechazado");
}

void test_stats() {
	StatSheet<4> sheet {};
	sheet.base[kAtk] = 10;
	sheet.base[kDef] = 5;
	check(!sheet.derived_valid, "stats: derivada pendiente al inicio");
	check(base_stat(sheet, kAtk) == 10 && base_stat(sheet, 9u) == 0, "stats: base_stat");
	check(derived_stat(sheet, kAtk) == 0, "stats: derivada a cero antes de recalcular");

	const eng::util::Array<StatMod, 3> mods {{StatMod {kAtk, 3, 0}, StatMod {kDef, 5, 0},
						  StatMod {kAtk, 0, 50}}};
	recompute_stats(sheet, mods.span());
	check(sheet.derived_valid, "stats: derivada valida tras recalcular");
	// ataque = (10 + 3) * 150 / 100 = 19 (division entera); defensa = 5 + 5 = 10.
	check(derived_stat(sheet, kAtk) == 19, "stats: suma + porcentaje acumulado");
	check(derived_stat(sheet, kDef) == 10, "stats: suma plana");
	check(derived_stat(sheet, 2u) == 0, "stats: atributo sin mods vale la base (0)");

	// Mods fuera de rango se ignoran.
	const eng::util::Array<StatMod, 2> stray {{StatMod {9u, 100, 0}, StatMod {no_stat, 50, 25}}};
	recompute_stats(sheet, stray.span());
	check(derived_stat(sheet, kAtk) == 10 && derived_stat(sheet, kDef) == 5,
	      "stats: mods fuera de rango ignorados");

	// Saturacion: adiciones enormes se recortan a s16 antes del porcentaje.
	const eng::util::Array<StatMod, 2> huge {{StatMod {kAtk, 32767, 0}, StatMod {kAtk, 32767, 100}}};
	recompute_stats(sheet, huge.span());
	check(derived_stat(sheet, kAtk) == 32767, "stats: saturacion a s16");

	// Porcentajes negativos: se acumulan y se recortan a -100 (factor 0 -> valor 0).
	const eng::util::Array<StatMod, 2> drain {{StatMod {kDef, 0, -80}, StatMod {kDef, 0, -80}}};
	recompute_stats(sheet, drain.span());
	check(derived_stat(sheet, kDef) == 0, "stats: porcentaje recortado a -100 da 0");
}

} // namespace

int main() {
	std::printf("RPG inventario/equipo/stats:\n");
	test_bag();
	test_equipment();
	test_stats();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG inventario/equipo/atributos derivados\n");
	return 0;
}
