// ============================================================================
// Test HOST-172: coordinacion de manadas (roles, flanqueo, llamada de caza)
// ============================================================================
//
// Valida `eng/sim/pack.hpp` y `SimWorld::coordinate_packs`:
//
//   1) `pack_role_for`/`flank_goal`: reparto de roles y puntos de flanqueo que envuelven
//      (los flanqueadores se reparten a ambos lados).
//   2) `coordinate_packs`: el lider con presa percibida orienta a los miembros (relaciones
//      `Pack` hacia el) a posiciones de flanqueo y los pone a cazar.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/172_sim_pack

#include <cstdio>

#include <eng/sim/pack.hpp>
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

void test_roles_and_goals() {
	check(pack_role_for(true, 0) == PackRole::Leader, "manada: el lider manda");
	check(pack_role_for(false, 0) == PackRole::Flanker &&
		      pack_role_for(false, 1) == PackRole::Follower,
	      "manada: roles rotatorios de los miembros");

	const eng::Point2s target {10, 10};
	const eng::Point2s even = flank_goal(target, PackRole::Flanker, 0u, 3u);
	const eng::Point2s odd = flank_goal(target, PackRole::Flanker, 1u, 3u);
	check(even.y != odd.y && even.x == target.x && odd.x == target.x,
	      "manada: los flanqueadores se reparten a ambos lados");
	check(flank_goal(target, PackRole::Follower, 0u, 3u).x < target.x,
	      "manada: el seguidor va detras");
}

void test_world_coordination() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	const EntityId leader = w.spawn(2u, 0u, 0u, 0, 0);
	const EntityId f1 = w.spawn(2u, 0u, 0u, 1, 0);
	const EntityId f2 = w.spawn(2u, 0u, 0u, 1, 1);
	const EntityId lone = w.spawn(1u, 1u, 0u, 5, 5); // otra faccion, sin pack
	w.find(leader)->set_realized(true);
	w.find(f1)->set_realized(true);
	w.find(f2)->set_realized(true);
	w.find(lone)->set_realized(true);
	set_relationship(w.find(f1)->relationships, leader, RelationKind::Pack, 80, 40);
	set_relationship(w.find(f2)->relationships, leader, RelationKind::Pack, 80, 40);

	// Sin presa percibida no hay coordinacion.
	check(w.coordinate_packs() == 0u, "manada: sin presa no coordina");

	observe(w.find(leader)->trackers, TrackerKind::Prey, 9u, 0u, 5, 5, 200u, 0u);
	const eng::u8 n = w.coordinate_packs();
	check(n == 2u, "manada: el lider coordina a sus dos miembros");
	check(w.find(f1)->behavior == Behavior::Hunt && w.find(f2)->behavior == Behavior::Hunt,
	      "manada: los miembros pasan a cazar");
	check(confidence_of(w.find(f1)->trackers, 9u, TrackerKind::Prey) > 0u &&
		      confidence_of(w.find(f2)->trackers, 9u, TrackerKind::Prey) > 0u,
	      "manada: los miembros comparten el objetivo");
	check(confidence_of(w.find(lone)->trackers, 9u, TrackerKind::Prey) == 0u,
	      "manada: quien no es del grupo no se coordina");
}

} // namespace

int main() {
	std::printf("Sim pack:\n");
	test_roles_and_goals();
	test_world_coordination();

	if (g_fail == 0u) {
		std::printf("OK: Sim pack (roles, flanqueo, coordinacion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
