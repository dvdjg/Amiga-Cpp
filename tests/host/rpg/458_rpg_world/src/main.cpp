// ============================================================================
// Test HOST-458: entidades de mundo y objetos en el suelo (`eng::rpg`)
// ============================================================================
//
// Valida `engine/include/eng/rpg/world.hpp`:
//
//   1) `EntityStore`: alta con tipo/verbos/posición, búsqueda por id, baja, reciclado de
//      huecos con **id nuevo** (los ids no se reutilizan), capacidad y recorrido.
//   2) `WorldEntity`: máscara de verbos e `interaction`/`entity_flags` como capacidades.
//   3) `GroundItems`: colocar, extraer, lleno, limpieza y recorrido ordenado (la lista que
//      conecta la instancia del `ItemStore` con el mapa).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/rpg/458_rpg_world

#include <cstdio>

#include <eng/rpg/world.hpp>

namespace {

using namespace eng::rpg;

unsigned g_fail = 0;

void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_entity_store() {
	EntityStore<4> store;
	check(store.live_count() == 0u, "entidades: pool vacio al inicio");
	check(!store.find(no_entity).valid(), "entidades: find de no_entity nulo");

	const EntityId chest = store.spawn(EntityKind::Container, 48, 160, /*room=*/2u,
					   interaction::open | interaction::take,
					   entity_flags::solid);
	check(chest != no_entity, "entidades: alta devuelve id");
	const EntityId anvil = store.spawn(EntityKind::Scenery, 240, 64, 2u, interaction::use);
	(void)store.spawn(EntityKind::Actor, 40, 120, 2u, interaction::talk);
	(void)store.spawn(EntityKind::Mount, 96, 200, 2u, interaction::mount);
	check(store.live_count() == 4u, "entidades: cuatro vivas");
	check(store.spawn(EntityKind::Trigger, 0, 0) == no_entity, "entidades: lleno no da id");

	eng::Ref<WorldEntity> c = store.find(chest);
	check(c.valid(), "entidades: find del cofre");
	check(c->kind == EntityKind::Container && c->x == 48 && c->y == 160 && c->room == 2u,
	      "entidades: campos del alta");
	check((c->verbs & interaction::open) != 0u && (c->verbs & interaction::use) == 0u,
	      "entidades: mascara de verbos");
	check((c->flags & entity_flags::solid) != 0u, "entidades: banderas de estado");

	eng::Ref<WorldEntity> a = store.find(anvil);
	check(a.valid() && a->kind == EntityKind::Scenery, "entidades: find del yunque");
	a->x = 241; // mutación permitida por la vista
	check(store.find(anvil)->x == 241, "entidades: mutacion via Ref persiste");

	check(store.remove(anvil), "entidades: baja");
	check(!store.find(anvil).valid(), "entidades: el id dado de baja no existe");
	check(!store.remove(anvil), "entidades: doble baja falla");
	check(store.live_count() == 3u, "entidades: live_count baja");

	// El hueco se recicla pero el id NO: un id viejo no puede resucitar a otra entidad.
	const EntityId door = store.spawn(EntityKind::Scenery, 304, 96, 2u, interaction::open);
	check(door != no_entity && door != anvil, "entidades: hueco reciclado con id nuevo");
	check(!store.find(anvil).valid(), "entidades: el id reciclado no revive el viejo");
	check(store.find(door)->kind == EntityKind::Scenery, "entidades: la entidad nueva responde");

	eng::u8 visited = 0u;
	eng::u8 open_verbs = 0u;
	store.for_each([&](const WorldEntity& e) {
		++visited;
		if ((e.verbs & interaction::open) != 0u) {
			++open_verbs;
		}
	});
	check(visited == 4u && open_verbs == 2u, "entidades: for_each recorre las vivas");

	store.clear();
	check(store.live_count() == 0u && !store.find(chest).valid(), "entidades: clear vacia el pool");
}

void test_ground_items() {
	ItemStore<4> items;
	ItemInstance potion {};
	potion.type = 1u;
	potion.qty = 2u;
	const ItemHandle h0 = items.spawn(potion);
	const ItemHandle h1 = items.spawn(potion);
	const ItemHandle h2 = items.spawn(potion);
	const ItemHandle h3 = items.spawn(potion);

	GroundItems<3> ground;
	check(ground.size() == 0u, "suelo: vacio al inicio");
	check(ground.drop(h0, 100, 64, 2u), "suelo: drop");
	check(ground.drop(h1, 120, 64), "suelo: drop con room por defecto");
	check(ground.drop(h2, 140, 64, 3u), "suelo: tercer drop");
	check(!ground.drop(h3, 160, 64), "suelo: lleno rechaza");
	check(ground.size() == 3u, "suelo: tres entradas");

	check(ground.at(0).item == h0 && ground.at(0).x == 100 && ground.at(0).room == 2u,
	      "suelo: primera entrada");
	check(ground.at(1).room == 0u, "suelo: room por defecto 0");
	check(ground.at(2).y == 64, "suelo: posicion de la tercera");

	const ItemHandle taken = ground.take_at(1);
	check(taken == h1, "suelo: take_at devuelve la instancia");
	check(ground.size() == 2u, "suelo: take_at compacta");
	check(ground.at(1).item == h2, "suelo: el orden se conserva");
	check(ground.take_at(9) == no_item, "suelo: take_at fuera de rango");
	check(!ground.full(), "suelo: hueco libre tras take");

	check(ground.drop(h3, 160, 64), "suelo: reutiliza el hueco");
	ground.clear();
	check(ground.size() == 0u, "suelo: clear");
	check(items.live_count() == 4u, "suelo: las instancias no se destruyen al limpiar");
}

} // namespace

int main() {
	std::printf("RPG world (entidades, suelo):\n");
	test_entity_store();
	test_ground_items();
	if (g_fail != 0u) {
		std::printf("FAIL: %u comprobacion(es)\n", g_fail);
		return 1;
	}
	std::printf("OK: RPG world (EntityStore/GroundItems)\n");
	return 0;
}
