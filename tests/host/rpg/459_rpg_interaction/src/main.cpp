// ============================================================================
// Test HOST-459: interaccion (cerraduras, recoger/soltar) y puente con la simulacion
// ============================================================================
//
// Valida `engine/include/eng/rpg/{interaction,sim_bridge}.hpp`:
//
//   1) `LockSet`: cerradura con llave por **etiqueta + metadato de instancia** (no una clase
//      por llave), idempotencia, rechazo de no-llaves y handles caducados.
//   2) `pick_up`/`drop_item`: apilado con resto que se queda en el suelo, bolsa llena,
//      entradas caducadas y devolucion si el suelo no admite.
//   3) `consume` y el puente `eat_item`/`rest_with_item`: comer consume una unidad y alimenta
//      `eng::sim::Needs` (la simulacion propone, el RPG interpreta); no-comida no se consume.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/459_rpg_interaction

#include <cstdio>

#include <eng/rpg/interaction.hpp>
#include <eng/rpg/sim_bridge.hpp>
#include <eng/sim/needs.hpp>

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
	ItemTypeId sword = no_item_type;
	ItemTypeId potion = no_item_type;
	ItemTypeId key = no_item_type;

	Fixture() {
		ItemDef sword_def {};
		sword_def.tags = item_tags::weapon;
		sword = catalog.add(sword_def);
		ItemDef potion_def {};
		potion_def.tags = item_tags::food | item_tags::drink;
		potion_def.stack_max = 5u;
		potion = catalog.add(potion_def);
		ItemDef key_def {};
		key_def.tags = item_tags::key;
		key = catalog.add(key_def);
	}

	ItemHandle spawn(ItemTypeId type, eng::u8 qty = 1u, eng::u16 meta = 0u) {
		ItemInstance inst {};
		inst.type = type;
		inst.qty = qty;
		inst.metadata = meta;
		return store.spawn(inst);
	}
};

void test_locks() {
	Fixture fx;
	LockSet<3> locks;
	const eng::u8 inn_door = locks.add(7u);  // se abre con la llave de metadato 7
	const eng::u8 cellar = locks.add(9u);    // otra cerradura distinta
	check(inn_door == 0u && cellar == 1u, "cerraduras: indices de contenido");
	check(locks.add(1u) == 2u, "cerraduras: tercera");
	check(locks.add(1u) == 0xffu, "cerraduras: lleno");

	const ItemHandle wrong = fx.spawn(fx.key, 1u, 3u);     // llave de otra puerta
	const ItemHandle right = fx.spawn(fx.key, 1u, 7u);     // la llave correcta
	const ItemHandle notakey = fx.spawn(fx.potion, 1u, 7u); // metadato correcto pero no es llave
	const ItemHandle ghost = fx.spawn(fx.key, 1u, 7u);
	(void)fx.store.destroy(ghost);

	check(!locks.is_open(inn_door), "cerraduras: cerrada al inicio");
	check(!locks.try_open(inn_door, wrong, fx.store, fx.catalog),
	      "cerraduras: llave de otra puerta rechazada");
	check(!locks.try_open(inn_door, notakey, fx.store, fx.catalog),
	      "cerraduras: objeto sin etiqueta key rechazado");
	check(!locks.try_open(inn_door, ghost, fx.store, fx.catalog),
	      "cerraduras: handle caducado rechazado");
	check(!locks.try_open(9u, right, fx.store, fx.catalog),
	      "cerraduras: indice inexistente rechazado");
	check(locks.try_open(inn_door, right, fx.store, fx.catalog), "cerraduras: abre con su llave");
	check(locks.is_open(inn_door), "cerraduras: queda abierta");
	check(locks.try_open(inn_door, ghost, fx.store, fx.catalog),
	      "cerraduras: abierta es idempotente (no exige llave viva)");
	check(!locks.is_open(cellar), "cerraduras: la otra sigue cerrada");
	check(fx.store.get(right).valid(), "cerraduras: la llave no se consume");
}

void test_pick_and_drop() {
	Fixture fx;

	// Pila de 7 con stack_max 5 y un solo hueco libre: entra una pila completa (5) y el resto
	// (2) queda en el suelo (la división crea la instancia de 5; el origen guarda 2).
	ItemBag<2> bag {};
	GroundItems<4> ground {};
	const ItemHandle blocker = fx.spawn(fx.sword); // ocupa el hueco 1 de la bolsa
	check(bag.put(blocker), "recoger: preparacion (hueco ocupado)");
	const ItemHandle pile = fx.spawn(fx.potion, 7u);
	(void)ground.drop(pile, 100, 64);
	check(pick_up(ground, 0u, bag, fx.store, fx.catalog) == 2u,
	      "recoger: pila completa dentro y resto 2 en el suelo");
	check(ground.size() == 1u && fx.store.get(pile)->qty == 2u,
	      "recoger: la entrada del suelo conserva el resto");
	check(bag_count(bag, fx.store, fx.potion) == 5u, "recoger: 5 unidades en la bolsa");
	check(bag.full(), "recoger: la bolsa queda llena");
	check(pick_up(ground, 0u, bag, fx.store, fx.catalog) == 2u,
	      "recoger: bolsa llena no admite el resto");
	// Con un hueco libre, la segunda pasada entra entera.
	check(bag.remove(blocker), "recoger: liberar el hueco ocupado");
	check(pick_up(ground, 0u, bag, fx.store, fx.catalog) == 0u,
	      "recoger: segunda pasada entra entera");
	check(ground.size() == 0u, "recoger: suelo limpio");
	check(bag_count(bag, fx.store, fx.potion) == 7u, "recoger: las 7 unidades completas");

	// Bolsa de 2 con dos espadas no apilables: la tercera no entra.
	ItemBag<2> bag2 {};
	const ItemHandle s1 = fx.spawn(fx.sword);
	const ItemHandle s2 = fx.spawn(fx.sword);
	const ItemHandle s3 = fx.spawn(fx.sword);
	check(bag2.put(s1) && bag2.put(s2), "recoger: bolsa llena de espadas");
	(void)ground.drop(s3, 120, 64);
	check(pick_up(ground, 0u, bag2, fx.store, fx.catalog) == 1u,
	      "recoger: sin sitio queda la unidad en el suelo");
	check(ground.size() == 1u, "recoger: la entrada sigue en el suelo");

	// Entrada caducada: se limpia sola.
	const ItemHandle ghost = fx.spawn(fx.sword);
	(void)ground.drop(ghost, 140, 64);
	(void)fx.store.destroy(ghost);
	check(pick_up(ground, 1u, bag2, fx.store, fx.catalog) == 0u,
	      "recoger: entrada caducada se limpia");
	check(ground.size() == 1u, "recoger: solo queda la espada s3");

	// Soltar: la espada del suelo entra a la mano y de ahí puede volver al suelo.
	check(bag2.remove(s1), "soltar: preparacion");
	check(pick_up(ground, 0u, bag2, fx.store, fx.catalog) == 0u && bag2.contains(s3),
	      "soltar: preparacion (s3 recogida)");
	check(drop_item(bag2, ground, s3, 88, 40, 1u), "soltar: la espada va al suelo");
	check(!bag2.contains(s3) && ground.size() == 1u, "soltar: sale de la bolsa y entra al suelo");
	check(!drop_item(bag2, ground, s3, 0, 0), "soltar: handle que no esta en la bolsa");

	GroundItems<1> tiny {};
	const ItemHandle s4 = fx.spawn(fx.sword);
	check(bag2.put(s4), "soltar: preparacion de suelo lleno");
	(void)tiny.drop(s3, 0, 0);
	check(!drop_item(bag2, tiny, s4, 10, 10), "soltar: suelo lleno falla");
	check(bag2.contains(s4), "soltar: el objeto no se pierde si el suelo esta lleno");
}

void test_consume_semantics() {
	Fixture fx;
	ItemBag<3> bag {};
	const ItemHandle potion = fx.spawn(fx.potion, 3u);
	(void)bag_add(bag, fx.store, fx.catalog, potion);

	check(!consume(bag, fx.store, potion, 4u), "consumo: mas unidades de las que hay");
	check(fx.store.get(potion)->qty == 3u && bag.contains(potion), "consumo: fallo no muta");
	check(consume(bag, fx.store, potion, 1u), "consumo: una unidad");
	check(fx.store.get(potion)->qty == 2u, "consumo: queda el resto");
	check(consume(bag, fx.store, potion, 2u), "consumo: las dos ultimas");
	check(!fx.store.get(potion).valid() && !bag.contains(potion),
	      "consumo: al llegar a 0 se destruye y sale de la bolsa");
}

void test_sim_bridge() {
	Fixture fx;
	ItemBag<2> bag {};
	const ItemHandle potion = fx.spawn(fx.potion, 2u);
	const ItemHandle sword = fx.spawn(fx.sword);
	(void)bag_add(bag, fx.store, fx.catalog, potion);
	(void)bag_add(bag, fx.store, fx.catalog, sword);

	eng::sim::Needs needs {};
	needs.hunger = 100u;
	needs.fatigue = 80u;

	check(!eat_item(bag, fx.store, fx.catalog, sword, needs, 50u),
	      "puente: la espada no es comida (nada consumido)");
	check(fx.store.get(sword).valid() && needs.hunger == 100u, "puente: rechazo sin efecto");

	check(eat_item(bag, fx.store, fx.catalog, potion, needs, 80u), "puente: comer la pocima");
	check(needs.hunger == 20u, "puente: la sim ha recibido la nutricion");
	check(fx.store.get(potion)->qty == 1u, "puente: se consumio una unidad");

	check(eat_item(bag, fx.store, fx.catalog, potion, needs, 80u), "puente: segunda unidad");
	check(!fx.store.get(potion).valid() && !bag.contains(potion),
	      "puente: la ultima unidad destruye la instancia");
	check(!eat_item(bag, fx.store, fx.catalog, potion, needs, 80u),
	      "puente: sin instancia no hay efecto");

	check(rest_with_item(bag, fx.store, sword, needs, 60u), "puente: descansar consume el objeto");
	check(needs.fatigue == 20u, "puente: la fatiga baja");
}

} // namespace

int main() {
	std::printf("RPG interaction (cerraduras, suelo, puente sim):\n");
	test_locks();
	test_pick_and_drop();
	test_consume_semantics();
	test_sim_bridge();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG interaction (LockSet/pick-up/drop/consume/bridge)\n");
	return 0;
}
