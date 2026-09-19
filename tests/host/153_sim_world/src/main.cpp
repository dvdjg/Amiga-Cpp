// ============================================================================
// Test HOST-153: mundo de simulacion y LOD abstracto/realizado (`eng::sim`)
// ============================================================================
//
// Valida `eng/sim/world.hpp` (y `society.hpp`):
//
//   1) Poblacion: `spawn`/`find` con ids correlativos, `no_entity` al llenarse.
//   2) Grafo de habitaciones: enlaces bidireccionales, adyacencia y grado.
//   3) LOD realizado: `realize_room` marca hasta `max` criaturas de la room y deja
//      abstractas las demas; `realized_count`/`clear_realized`.
//   4) `tick_realized`: avanza necesidades, olvida trackers y elige comportamiento.
//   5) `tick_abstract` escalonado: cada llamada procesa 1/`stagger_period`; migracion
//      al refugio con lluvia, flag `in_den` y muerte por hambre.
//   6) Sociedad: reputacion por faccion (recorte, hostilidad/amistad) y `Pack`.
//   7) Determinismo: mismo estado + misma semilla => mismo resultado.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/153_sim_world

#include <cstdio>

#include <eng/core/random.hpp>
#include <eng/sim/domain.hpp>
#include <eng/sim/inventory.hpp>
#include <eng/sim/lifecycle.hpp>
#include <eng/sim/object.hpp>
#include <eng/sim/society.hpp>
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

using World = SimWorld<SimTraits, 8, 4, 4, 8>;

// 1) Poblacion ---------------------------------------------------------------
void test_population() {
	SimWorld<SimTraits, 2, 3, 3, 4> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 10, 20);
	const EntityId b = w.spawn(2u, 0u, 0u, 30, 40);
	const EntityId full = w.spawn(3u, 0u, 0u, 0, 0);
	check(a == 1u && b == 2u, "world: ids correlativos desde 1");
	check(full == no_entity, "world: sin sitio -> no_entity");
	check(w.creature_count() == 2u, "world: cuenta de criaturas");

	const auto* found = w.find(a);
	check(found != nullptr && found->id == a && found->species == 1u,
	      "world: find por id");
	check(w.find(no_entity) == nullptr && w.find(static_cast<EntityId>(a + 40u)) == nullptr,
	      "world: find de id invalido da nullptr");
}

// 2) Grafo de habitaciones ----------------------------------------------------
void test_rooms() {
	World w;
	w.link_rooms(0u, 1u);
	w.link_rooms(1u, 2u);
	check(w.rooms_adjacent(0u, 1u) && w.rooms_adjacent(1u, 0u),
	      "rooms: enlace bidireccional");
	check(w.rooms_adjacent(1u, 2u) && !w.rooms_adjacent(0u, 2u),
	      "rooms: adyacencia correcta");
	check(w.room_degree(1u) == 2u && w.room_degree(0u) == 1u, "rooms: grado");
	check(w.room_link(0u, 0u) == 1u && w.room_link(0u, 1u) == no_room,
	      "rooms: acceso a vecinos con centinela");
}

// 3) LOD realizado ------------------------------------------------------------
void test_realize() {
	World w;
	(void)w.spawn(1u, 0u, 0u, 0, 0);
	(void)w.spawn(2u, 0u, 0u, 0, 0);
	(void)w.spawn(3u, 0u, 0u, 0, 0);
	(void)w.spawn(1u, 0u, 1u, 0, 0); // en otra room

	w.realize_room(0u, 2u);
	check(w.realized_count() == 2u, "realize: marca hasta el maximo de la room");
	check(w.creature(3u).realized() == false, "realize: la sobrante queda abstracta");
	check(w.creature(3u).room == 1u && w.creature(3u).realized() == false,
	      "realize: otra room no se toca");

	w.realize_room(0u, 1u);
	check(w.realized_count() == 1u, "realize: reduce el conjunto realizado");

	w.clear_realized();
	check(w.realized_count() == 0u, "realize: clear");
}

// 4) Tick realizado -----------------------------------------------------------
void test_tick_realized() {
	World w;
	const EntityId id = w.spawn(1u, 0u, 0u, 100, 100);
	World::Creature* c = w.find(id);
	check(c != nullptr, "tick_realized: criatura creada");
	c->needs.hunger = 200;
	observe(c->trackers, TrackerKind::Threat, 77u, 0u, 110, 100, 5, 0u);
	c->set_realized(true);

	eng::Xoroshiro64pp rng {1u, 2u};
	w.tick_realized(rng);

	check(c->needs.hunger == 201u, "tick_realized: el hambre avanza");
	check(c->trackers[0].confidence == 4u, "tick_realized: los trackers se olvidan");
	check(static_cast<eng::u8>(c->behavior) < static_cast<eng::u8>(Behavior::Count),
	      "tick_realized: se elige un comportamiento valido");
	check(c->alive(), "tick_realized: la criatura sigue viva");
}

// 5) Tick abstracto escalonado, migracion y muerte ---------------------------
void test_tick_abstract() {
	World w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId b = w.spawn(2u, 0u, 0u, 0, 0);
	const EntityId c = w.spawn(3u, 0u, 0u, 0, 0);
	const EntityId d = w.spawn(4u, 0u, 0u, 0, 0);
	eng::Xoroshiro64pp rng {3u, 5u};

	// `stagger_period` por defecto = 4: la primera llamada solo procesa id%4==0 (id 4).
	w.tick_abstract(rng);
	check(w.find(a)->needs.hunger == 0u && w.find(b)->needs.hunger == 0u &&
		      w.find(c)->needs.hunger == 0u && w.find(d)->needs.hunger == 1u,
	      "abstract: tick escalonado reparte el trabajo");

	for (int i = 0; i < 3; ++i) {
		w.tick_abstract(rng);
	}
	check(w.find(a)->needs.hunger == 1u && w.find(b)->needs.hunger == 1u &&
		      w.find(c)->needs.hunger == 1u && w.find(d)->needs.hunger == 1u,
	      "abstract: tras un periodo completo todas avanzan una vez");

	// Migracion al refugio con lluvia: id 1 en room 0, refugio en room 1 (adyacente).
	w.link_rooms(0u, 1u);
	World::Creature* ca = w.find(a);
	ca->set_den(1u, 0, 0);
	w.set_rain(true);
	for (int i = 0; i < 5; ++i) {
		w.tick_abstract(rng);
	}
	check(ca->room == 1u && ca->at_home() && (ca->flags & flags::in_den) != 0u,
	      "abstract: con lluvia migra al refugio y marca in_den");

	// Muerte por hambre: al llegar a 255 en el tick abstracto se muere.
	SimWorld<SimTraits, 2, 2, 2, 4> dead_world;
	const EntityId e = dead_world.spawn(1u, 0u, 0u, 0, 0);
	dead_world.find(e)->needs.hunger = 254u;
	eng::Xoroshiro64pp rng2 {9u, 9u};
	// id 1 % 4 == 1: se procesa en la segunda llamada.
	dead_world.tick_abstract(rng2);
	dead_world.tick_abstract(rng2);
	check(!dead_world.find(e)->alive(), "abstract: el hambre extrema mata");
}

// 6) Sociedad -----------------------------------------------------------------
void test_society() {
	Society s;
	check(s.neutral(0u), "society: arranca neutral");
	s.adjust(0u, 50);
	check(s.rep(0u) == 50 && s.friendly(0u), "society: reputacion alta da amistad");
	s.adjust(1u, -60);
	check(s.rep(1u) == -60 && s.hostile(1u), "society: reputacion baja da hostilidad");
	s.adjust(2u, 200);
	check(s.rep(2u) == 100, "society: recorta a +100");
	s.adjust(0u, -300);
	check(s.rep(0u) == -100, "society: recorta a -100");
	check(s.rep(200u) == 0, "society: faccion fuera de rango es neutral");

	Pack p;
	check(p.join(1u) && p.leader == 1u, "pack: el primero es lider");
	check(p.join(2u) && p.join(3u), "pack: se unen miembros");
	check(p.contains(2u) && p.size() == 3u, "pack: contiene y cuenta");
	p.leave(1u);
	check(p.leader == 2u && p.size() == 2u, "pack: al irse el lider asciende el siguiente");
	p.leave(2u);
	p.leave(3u);
	check(p.leader == no_entity && p.size() == 0u, "pack: se vacia");
}

// 7) Determinismo -------------------------------------------------------------
void test_determinism() {
	auto run = [] {
		World w;
		(void)w.spawn(1u, 0u, 0u, 10, 10);
		(void)w.spawn(2u, 0u, 0u, 20, 20);
		w.realize_room(0u, 8u);
		eng::Xoroshiro64pp rng {42u, 43u};
		w.set_rain(true);
		for (int i = 0; i < 6; ++i) {
			w.tick_realized(rng);
			w.tick_abstract(rng);
		}
		return w;
	};
	const World w1 = run();
	const World w2 = run();
	check(w1.creature(0u).behavior == w2.creature(0u).behavior &&
		      w1.creature(0u).needs.hunger == w2.creature(0u).needs.hunger,
	      "determinismo: mismo estado y semilla -> mismo resultado");
}

// 8) Ciclo de vida en el mundo -----------------------------------------------
void test_world_lifecycle() {
	SimWorld<SimTraits, 16, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId b = w.spawn(1u, 0u, 0u, 1, 0);
	World::Creature* ca = w.find(a);
	World::Creature* cb = w.find(b);
	ca->age = 120u;
	cb->age = 120u;
	ca->set_realized(true);
	cb->set_realized(true);
	set_relationship(ca->relationships, b, RelationKind::Mate, 80, 80);
	set_relationship(cb->relationships, a, RelationKind::Mate, 80, 80);

	eng::Xoroshiro64pp rng {9u, 10u};
	const EntityId initiated = w.try_reproduce(rng);
	check(initiated == a && w.find(a)->repro.gestating(),
	      "world: la pareja inicia la gestacion");

	eng::u8 births = 0u;
	for (int i = 0; i < 200 && w.creature_count() < 3u; ++i) {
		births = static_cast<eng::u8>(births + w.advance_reproduction(rng));
	}
	check(w.creature_count() == 3u && births == 1u,
	      "world: la cria nace al terminar la gestacion");
	const EntityId child = w.creature(2u).id;
	check(w.find(child)->age == 0u, "world: la cria nace en la infancia");
	check(w.find(child)->genome.gene(Gene::Aggression) <= 100u, "world: la cria hereda genoma");
	check(w.find(a)->needs.hunger > 0u || w.find(b)->needs.hunger > 0u,
	      "world: criar cuesta recursos");

	// Vejez: al llegar a la edad limite, la criatura muere.
	ca->age = 255u;
	w.tick_realized(rng);
	check(!w.find(a)->alive(), "world: la vejez mata");
}

// 11) Objetos y economia del mundo -------------------------------------------
void test_world_objects() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const auto acts = ConstructionDomain::actions();
	check(w.replan(a, start_state(SimInventory {}), ConstructionDomain::goal(false, true),
		       acts.span()),
	      "objetos: plan de refugio");

	eng::u8 steps = 0u;
	while (w.has_plan(a) && steps < 8u) {
		const SimActionKind k = action_kind_of(w.current_action(a));
		(void)w.execute_action(a, k);
		w.advance_plan(a);
		++steps;
	}
	check(w.items().count_kind(ItemKind::Shelter) == 1u,
	      "objetos: el plan deja un refugio en el mundo");
	check(w.find(a)->carrying.count(ItemKind::Tool) == 0u &&
		      w.find(a)->carrying.count(ItemKind::Material) == 0u,
	      "objetos: se consumen los materiales al construir");

	const eng::s16 gain = w.offer_gift(0u, 1u, ItemKind::Food, 10u);
	check(gain > 0 && w.society().friendly(1u), "economia: el regalo sube la reputacion");
	check(w.economy().demand_of(ItemKind::Food) >= 10u,
	      "economia: el regalo sube la demanda");
}

// 12) Tend: el adulto ensena a la cria ---------------------------------------
void test_tend_share() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId parent = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId kid = w.spawn(1u, 0u, 0u, 1, 0);
	World::Creature* p = w.find(parent);
	p->set_realized(true);
	p->personality.empathy = 100u;
	learn(p->knowledge, KnowledgeKind::FoodSource, 3u, 255u);
	observe(p->trackers, TrackerKind::Kin, kid, 0u, 1, 0, 255, 0u);

	eng::Xoroshiro64pp rng {3u, 3u};
	w.tick_realized(rng);
	check(p->behavior == Behavior::Tend, "tend: el adulto dedica el tick a la cria");
	check(confidence_for(w.find(kid)->knowledge, KnowledgeKind::FoodSource, 3u) >= 128u,
	      "tend: la cria aprende del adulto");
}

// 9) Puesta de la reina (enjambre) -------------------------------------------
void test_brood() {
	SimWorld<SimTraits, 16, 4, 4, 8> w;
	Genome queen_g {};
	queen_g.set(Gene::Size, 90u);
	const EntityId q = w.spawn(2u, 0u, 0u, 5, 5);
	w.find(q)->genome = queen_g;
	w.find(q)->set_realized(true);

	eng::Xoroshiro64pp rng {4u, 4u};
	const EntityId egg = w.lay_brood(rng);
	check(egg != no_entity && w.creature_count() == 2u, "colonia: la reina pone un huevo");
	check(caste_of(w.find(egg)->genome) == Caste::Worker,
	      "colonia: sin censo, la puesta es obrera");
	check(w.colony().population() == 1u, "colonia: el censo registra la puesta");
}

// 10) Planificacion integrada en el mundo ------------------------------------
void test_world_planning() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId a = w.spawn(1u, 0u, 0u, 0, 0);
	const auto acts = ConstructionDomain::actions();
	const SimGoap::Goal goal = ConstructionDomain::goal(false, true);

	check(w.replan(a, start_state(SimInventory {}), goal, acts.span()),
	      "world: plan asignado");
	check(w.has_plan(a) && w.planning_count() == 1u, "world: hay un plan activo");
	check(action_kind_of(w.current_action(a)) == SimActionKind::Gather,
	      "world: el plan empieza por recoger");
	w.advance_plan(a);
	w.advance_plan(a);
	w.advance_plan(a);
	w.advance_plan(a);
	check(!w.has_plan(a) && w.planning_count() == 0u, "world: plan consumido");
	check(!w.replan(no_entity, start_state(SimInventory {}), goal, acts.span()),
	      "world: id invalido no planifica");
}

} // namespace

int main() {
	std::printf("Sim world:\n");
	test_population();
	test_rooms();
	test_realize();
	test_tick_realized();
	test_tick_abstract();
	test_society();
	test_determinism();
	test_world_lifecycle();
	test_brood();
	test_world_planning();
	test_world_objects();
	test_tend_share();

	if (g_fail == 0u) {
		std::printf("OK: Sim world (poblacion, LOD, ticks, migracion, sociedad, ciclo de "
			    "vida, puesta, planificacion, objetos, tend)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
