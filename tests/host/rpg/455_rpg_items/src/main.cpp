// ============================================================================
// Test HOST-455: objetos con instancia (`eng::rpg`) -- catalogo, pool y apilado
// ============================================================================
//
// Valida `engine/include/eng/rpg/{types,items}.hpp`:
//
//   1) Etiquetas (`item_tags`, `has_tags`/`has_any_tag`): mascaras de capacidad, no clases.
//   2) `ItemCatalog`: registro de tipos, rango valido y rechazo al llenarse.
//   3) `ItemStore`: pool de instancias con handles de generacion -- reciclado del hueco,
//      caducidad de handles viejos, doble destroy, `restore` (carga) y recorrido ordenado.
//   4) Apilado: `stack_compatible` (el estado separa pilas) y `merge_stack` (mueve lo que
//      cabe y devuelve el resto).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/455_rpg_items

#include <cstdio>

#include <eng/rpg/items.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_tags() {
	const eng::u16 potion_tags = item_tags::food | item_tags::drink;
	check(has_tags(potion_tags, item_tags::food), "etiquetas: subconjunto presente");
	check(has_tags(potion_tags, item_tags::food | item_tags::drink),
	      "etiquetas: varias presentes");
	check(!has_tags(potion_tags, item_tags::weapon), "etiquetas: ausente");
	check(has_tags(potion_tags, 0u), "etiquetas: mascara vacia siempre cumple");
	check(has_any_tag(potion_tags, item_tags::weapon | item_tags::drink),
	      "etiquetas: alguna presente");
	check(!has_any_tag(potion_tags, item_tags::weapon | item_tags::key),
	      "etiquetas: ninguna presente");
}

void test_catalog() {
	ItemCatalog<3> catalog;
	check(catalog.count == 0u && !catalog.valid(0u), "catalogo: vacio al inicio");
	check(!catalog.at(0u).valid(), "catalogo: at de tipo no registrado es nulo");

	ItemDef sword {};
	sword.tags = item_tags::weapon;
	sword.weight = 3u;
	sword.durability_max = 100u;
	sword.mod_first = 0u;
	sword.mod_count = 1u;
	const ItemTypeId sword_id = catalog.add(sword);
	check(sword_id == 0u, "catalogo: primer id es 0");
	check(catalog.valid(0u) && catalog.at(0u)->tags == item_tags::weapon,
	      "catalogo: definicion registrada");

	ItemDef potion {};
	potion.tags = item_tags::food | item_tags::drink;
	potion.stack_max = 5u;
	potion.weight = 1u;
	const ItemTypeId potion_id = catalog.add(potion);
	check(potion_id == 1u && catalog.valid(1u), "catalogo: segundo id es 1");
	check(catalog.at(1u)->stack_max == 5u, "catalogo: campo leido por puntero");

	(void)catalog.add(sword);
	check(catalog.count == 3u, "catalogo: lleno");
	check(catalog.add(sword) == no_item_type, "catalogo: rechazo al llenarse");
}

void test_store() {
	ItemStore<4> store;
	check(store.live_count() == 0u, "pool: vacio al inicio");
	check(!store.get(no_item).valid(), "pool: handle vacio no tiene instancia");

	ItemInstance basic {};
	basic.type = 0u;
	basic.qty = 1u;
	basic.durability = 100u;

	const ItemHandle h0 = store.spawn(basic);
	check(h0.valid() && h0.slot == 0u && h0.generation == 1u,
	      "pool: primer spawn en slot 0 con generacion 1");
	check(store.get(h0).valid() && store.get(h0)->durability == 100u,
	      "pool: instancia accesible por handle");
	(void)store.spawn(basic);
	(void)store.spawn(basic);
	(void)store.spawn(basic);
	check(store.live_count() == 4u, "pool: cuatro vivas");
	check(!store.spawn(basic).valid(), "pool: lleno devuelve handle invalido");

	check(store.destroy(h0), "pool: destroy de handle vivo");
	check(!store.get(h0).valid(), "pool: handle destruido no tiene instancia");
	check(store.live_count() == 3u, "pool: live_count baja");

	const ItemHandle h4 = store.spawn(basic);
	check(h4.valid() && h4.slot == 0u && h4.generation == 2u,
	      "pool: hueco reciclado con generacion nueva");
	check(!store.get(h0).valid(), "pool: handle viejo caduca al reciclar");
	check(store.get(h4).valid() && store.get(h4)->durability == 100u,
	      "pool: handle nuevo vivo con la instancia nueva");

	store.get(h4)->qty = 7u;
	check(store.get(h4)->qty == 7u, "pool: mutacion via Ref");

	check(store.destroy(h4) && !store.destroy(h4), "pool: doble destroy");
	check(!store.destroy(h0), "pool: destroy con handle caducado");
	check(!store.destroy(no_item), "pool: destroy de handle vacio");

	store.clear();
	check(store.live_count() == 0u, "pool: clear vacia el pool");
	ItemInstance restored {};
	restored.type = 1u;
	restored.qty = 3u;
	check(store.restore(2u, 5u, restored), "pool: restore exacto");
	check(store.get(ItemHandle {2u, 5u}).valid() && store.get(ItemHandle {2u, 5u})->type == 1u,
	      "pool: instancia restaurada accesible");
	check(!store.restore(2u, 6u, restored), "pool: restore en hueco ocupado falla");
	check(!store.restore(250u, 1u, restored), "pool: restore fuera de rango falla");
	check(!store.restore(1u, 0u, restored), "pool: restore con generacion 0 falla");

	eng::u8 visited = 0u;
	store.for_each_live([&](ItemHandle, const ItemInstance&) { ++visited; });
	check(visited == 1u, "pool: for_each_live recorre las vivas");
}

void test_stacking() {
	ItemDef potion {};
	potion.stack_max = 5u;

	ItemInstance dst {};
	dst.type = 1u;
	dst.qty = 3u;
	ItemInstance src {};
	src.type = 1u;
	src.qty = 4u;

	check(stack_compatible(dst, src), "apilado: misma pila compatible");
	check(merge_stack(dst, src, potion) == 2u, "apilado: resto 2 tras llenar");
	check(dst.qty == 5u && src.qty == 2u, "apilado: mueve solo lo que cabe");
	check(merge_stack(dst, src, potion) == 2u, "apilado: pila llena no acepta mas");

	ItemDef sword {};
	sword.stack_max = 1u;
	check(merge_stack(dst, src, sword) == 2u, "apilado: no apilable no fusiona");
	check(dst.qty == 5u, "apilado: destino intacto si no apilable");

	ItemInstance worn {};
	worn.type = 1u;
	worn.qty = 2u;
	worn.durability = 50u;
	ItemInstance mint {};
	mint.type = 1u;
	mint.qty = 2u;
	check(!stack_compatible(worn, mint), "apilado: durabilidad distinta separa pilas");
	check(merge_stack(worn, mint, potion) == 2u, "apilado: incompatibles no fusionan");

	mint.upgrade = 1u;
	mint.durability = 50u;
	check(!stack_compatible(worn, mint), "apilado: mejora distinta separa pilas");
	mint.upgrade = 0u;
	mint.affixes = 0x0004u;
	check(!stack_compatible(worn, mint), "apilado: afijos distintos separan pilas");
}

} // namespace

int main() {
	std::printf("RPG items (catalogo, pool de instancias, apilado):\n");
	test_tags();
	test_catalog();
	test_store();
	test_stacking();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG items (tags/catalogo/ItemStore/apilado)\n");
	return 0;
}
