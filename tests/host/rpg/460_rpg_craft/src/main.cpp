// ============================================================================
// Test HOST-460: crafteo por recetas (`eng::rpg/craft.hpp`)
// ============================================================================
//
// Valida el sistema de recetas del hito M2:
//
//   1) Ingredientes por **capacidad** (etiquetas) o tipo exacto, estación por máscara y
//      habilidad mínima: cada rechazo devuelve su `CraftStatus` sin tocar nada.
//   2) Consumo determinista repartido por pilas (dos pilas de mineral = un ingrediente de 2),
//      con destrucción de las instancias agotadas.
//   3) Transaccionalidad: el pool lleno (`StoreFull`), ingredientes que faltan o rangos mal
//      formados **no** consumen material; la salida nace con durabilidad completa.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/460_rpg_craft

#include <cstdio>

#include <eng/rpg/craft.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Fixture {
	ItemCatalog<4> catalog {};
	ItemStore<8> store {};
	eng::util::Array<Ingredient, 2> ingredients {};
	eng::util::Array<Recipe, 4> recipes {};
	RecipeBook book {};
	ItemTypeId ore = no_item_type;
	ItemTypeId sword = no_item_type;
	ItemTypeId potion = no_item_type;

	Fixture() {
		ItemDef ore_def {};
		ore_def.tags = item_tags::material;
		ore_def.stack_max = 20u;
		ore_def.weight = 2u;
		ore = catalog.add(ore_def);
		ItemDef sword_def {};
		sword_def.tags = item_tags::weapon;
		sword_def.durability_max = 100u;
		sword = catalog.add(sword_def);
		ItemDef potion_def {};
		potion_def.tags = item_tags::food;
		potion_def.stack_max = 5u;
		potion = catalog.add(potion_def);

		eng::u8 n = 0u;
		ingredients[n++] = Ingredient {no_item_type, item_tags::material, 2u}; // 0: espada
		ingredients[n++] = Ingredient {no_item_type, item_tags::material, 1u}; // 1: pocima

		recipes[0] = Recipe {item_tags::tool, sword, 1u, 2u, 0u, 1u};  // yunque, skill 2
		recipes[1] = Recipe {0u, potion, 1u, 0u, 1u, 1u};             // en cualquier sitio
		recipes[2] = Recipe {0u, potion, 1u, 0u, 5u, 1u};             // rango mal formado
		recipes[3] = Recipe {0u, 9u, 1u, 0u, 1u, 1u};                 // salida sin catalogo

		book.recipes = recipes.span();
		book.ingredients = ingredients.span();
	}

	ItemHandle spawn_ore(eng::u8 qty) {
		ItemInstance inst {};
		inst.type = ore;
		inst.qty = qty;
		return store.spawn(inst);
	}
};

void test_status_paths() {
	Fixture fx;
	ItemBag<3> bag {};
	ItemHandle produced {};

	check(craft(bag, fx.store, fx.catalog, fx.book, 9u, 0u, 9u, produced) == CraftStatus::BadRecipe,
	      "crafteo: indice fuera del libro");
	check(craft(bag, fx.store, fx.catalog, fx.book, 2u, 0u, 9u, produced) == CraftStatus::BadRecipe,
	      "crafteo: rango de ingredientes mal formado");
	check(craft(bag, fx.store, fx.catalog, fx.book, 3u, 0u, 0u, produced) ==
		  CraftStatus::UnknownOutput,
	      "crafteo: salida sin catalogo (antes de mirar ingredientes)");

	const ItemHandle ore_pile = fx.spawn_ore(3u);
	check(bag_add(bag, fx.store, fx.catalog, ore_pile) == 0u, "crafteo: mineral en la bolsa");

	check(craft(bag, fx.store, fx.catalog, fx.book, 0u, 0u, 5u, produced) ==
		  CraftStatus::MissingStation,
	      "crafteo: sin estacion");
	check(craft(bag, fx.store, fx.catalog, fx.book, 0u, item_tags::tool, 1u, produced) ==
		  CraftStatus::MissingSkill,
	      "crafteo: habilidad insuficiente");
	check(fx.store.get(ore_pile)->qty == 3u && !produced.valid(),
	      "crafteo: los rechazos no consumen");
	check(craft_status_name(CraftStatus::MissingStation)[0] == 'm', "crafteo: nombre del estado");

	// Éxito: yunque + skill 2.
	check(craft(bag, fx.store, fx.catalog, fx.book, 0u, item_tags::tool, 2u, produced) ==
		  CraftStatus::Done,
	      "crafteo: espada fabricada");
	check(produced.valid(), "crafteo: handle de salida");
	eng::Ref<ItemInstance> made = fx.store.get(produced);
	check(made.valid() && made->type == fx.sword && made->qty == 1u && made->durability == 100u,
	      "crafteo: salida con durabilidad completa");
	check(fx.store.get(ore_pile)->qty == 1u, "crafteo: consume exactamente 2 mineral");
	check(bag_count(bag, fx.store, fx.sword) == 0u, "crafteo: la salida sale del store, no de la bolsa");
	check(bag_add(bag, fx.store, fx.catalog, produced) == 0u && bag.contains(produced),
	      "crafteo: la salida entra en la bolsa (apilado del llamador)");

	// Sin estación: la pócima sale con el mineral que queda.
	check(craft(bag, fx.store, fx.catalog, fx.book, 1u, 0u, 0u, produced) == CraftStatus::Done,
	      "crafteo: receta sin estación");
	check(!fx.store.get(ore_pile).valid(), "crafteo: la última unidad agota la instancia");
	check(!bag.contains(ore_pile), "crafteo: la pila agotada sale de la bolsa");
	check(bag_add(bag, fx.store, fx.catalog, produced) == 0u, "crafteo: la pócima entra en la bolsa");
	check(bag_count(bag, fx.store, fx.potion) == 1u, "crafteo: pócima en la bolsa");

	check(craft(bag, fx.store, fx.catalog, fx.book, 0u, item_tags::tool, 2u, produced) ==
		  CraftStatus::MissingInputs,
	      "crafteo: sin mineral no hay espada");
}

void test_multi_stack_consumption() {
	Fixture fx;
	ItemBag<3> bag {};
	// Dos entradas del mismo tipo por colocación directa: `bag_add` fusionaría ambas en una
	// sola pila (es su trabajo), pero el juego puede tener pilas separadas (loot sin recoger).
	const ItemHandle a = fx.spawn_ore(1u);
	const ItemHandle b = fx.spawn_ore(1u);
	check(bag.put(a) && bag.put(b), "multi-pila: dos pilas de una unidad");
	check(bag.size() == 2u, "multi-pila: dos entradas");

	ItemHandle produced {};
	check(craft(bag, fx.store, fx.catalog, fx.book, 0u, item_tags::tool, 2u, produced) ==
		  CraftStatus::Done,
	      "multi-pila: receta de 2 desde dos pilas");
	check(!fx.store.get(a).valid() && !fx.store.get(b).valid(),
	      "multi-pila: ambas instancias agotadas");
	check(bag.size() == 0u, "multi-pila: bolsa vacía");
	check(fx.store.get(produced).valid(), "multi-pila: espada creada");
}

void test_store_full_is_transactional() {
	// Catálogo propio: material apilable y una salida con durabilidad, con un pool de 2 huecos.
	ItemCatalog<2> cat {};
	ItemDef mat_def {};
	mat_def.tags = item_tags::material;
	mat_def.stack_max = 9u;
	const ItemTypeId mat = cat.add(mat_def);
	ItemDef out_def {};
	out_def.tags = item_tags::weapon;
	out_def.durability_max = 50u;
	const ItemTypeId out = cat.add(out_def);

	ItemStore<2> store2;
	ItemInstance i1 {};
	i1.type = mat;
	i1.qty = 2u;
	const ItemHandle pile = store2.spawn(i1); // ocupa el hueco 0
	ItemInstance i2 {};
	i2.type = mat;
	i2.qty = 1u;
	(void)store2.spawn(i2); // ocupa el hueco 1: pool lleno
	check(store2.live_count() == 2u, "transaccional: pool lleno");

	eng::util::Array<Ingredient, 1> ings {};
	ings[0] = Ingredient {mat, 0u, 2u};
	eng::util::Array<Recipe, 1> recs {};
	recs[0] = Recipe {0u, out, 1u, 0u, 0u, 1u};
	RecipeBook book {recs.span(), ings.span()};

	ItemBag<2> bag2 {};
	check(bag_add(bag2, store2, cat, pile) == 0u, "transaccional: mineral en la bolsa");
	const eng::u8 before = store2.get(pile)->qty;
	ItemHandle produced {};
	check(craft(bag2, store2, cat, book, 0u, 0u, 0u, produced) == CraftStatus::StoreFull,
	      "transaccional: pool lleno rechaza");
	check(store2.get(pile)->qty == before && !produced.valid(),
	      "transaccional: el material queda intacto");
}

} // namespace

int main() {
	std::printf("RPG craft (recetas, estaciones, transaccionalidad):\n");
	test_status_paths();
	test_multi_stack_consumption();
	test_store_full_is_transactional();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG craft (RecipeBook/craft)\n");
	return 0;
}
