// ============================================================================
// Test HOST-113: arboles de comportamiento sin heap
// ============================================================================
//
// Valida `engine/include/eng/ai/decision/behavior_tree.hpp`:
//
//   1) Secuencia: falla en el primer hijo que falla (cortocircuito).
//   2) Selector: tiene exito en el primer hijo que lo logra.
//   3) Arbol vacio/sin raiz y selector con todos los hijos fallando.
//   4) Caso de uso: guardia que dispara si tiene municion o recarga si no.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/113_behavior_tree

#include <cstdio>

#include <eng/ai/decision/behavior_tree.hpp>

namespace {

using eng::u16;
using eng::ai::BtStatus;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

int g_ammo = 0;
bool g_fired = false;
bool g_reloaded = false;
bool g_second_called = false;

BtStatus has_ammo() { return g_ammo > 0 ? BtStatus::Success : BtStatus::Failure; }
BtStatus fire() { g_fired = true; --g_ammo; return BtStatus::Success; }
BtStatus reload() { g_reloaded = true; g_ammo = 3; return BtStatus::Success; }
BtStatus always_fail() { return BtStatus::Failure; }
BtStatus second() { g_second_called = true; return BtStatus::Success; }

void test_guard_tree() {
	g_ammo = 2;
	g_fired = false;
	g_reloaded = false;

	eng::ai::BehaviorTree<6> bt;
	const u16 has = bt.add_leaf(has_ammo);
	const u16 fire_n = bt.add_leaf(fire);
	const u16 shoot = bt.add_sequence(has, 2); // has_ammo -> fire
	const u16 reload_n = bt.add_leaf(reload);
	const u16 root = bt.add_selector(shoot, 2); // shoot | reload
	bt.set_root(root);
	check(bt.node_count() == 5u, "bt: 5 nodos");
	check(fire_n != eng::ai::BehaviorTree<6>::no_node &&
		      reload_n != eng::ai::BehaviorTree<6>::no_node,
	      "bt: hojas validas");

	check(bt.tick() == BtStatus::Success, "bt: con municion tiene exito");
	check(g_fired && !g_reloaded, "bt: la rama de disparo gana");

	// Sin municion: la secuencia falla en `has_ammo` y el selector recarga.
	g_ammo = 0;
	g_fired = false;
	check(bt.tick() == BtStatus::Success, "bt: sin municion recarga");
	check(!g_fired && g_reloaded, "bt: la rama de recarga gana");
}

void test_composites() {
	g_second_called = false;
	eng::ai::BehaviorTree<4> bt;
	const u16 fail = bt.add_leaf(always_fail);
	const u16 ok = bt.add_leaf(second);
	const u16 seq = bt.add_sequence(fail, 2); // falla en el primero -> no llega al segundo
	bt.set_root(seq);
	check(ok != eng::ai::BehaviorTree<4>::no_node, "bt: la segunda hoja existe");
	check(bt.tick() == BtStatus::Failure, "bt: secuencia falla");
	check(!g_second_called, "bt: la secuencia cortocircuita");

	eng::ai::BehaviorTree<4> all_fail;
	const u16 f1 = all_fail.add_leaf(always_fail);
	const u16 f2 = all_fail.add_leaf(always_fail);
	const u16 sel = all_fail.add_selector(f1, 2);
	check(f2 != eng::ai::BehaviorTree<4>::no_node, "bt: el segundo hijo existe");
	all_fail.set_root(sel);
	check(all_fail.tick() == BtStatus::Failure, "bt: selector con todos fallando falla");

	eng::ai::BehaviorTree<2> empty;
	check(empty.tick() == BtStatus::Failure, "bt: arbol vacio falla");
}

void test_capacity() {
	eng::ai::BehaviorTree<2> bt;
	auto task = []() { return BtStatus::Success; };
	check(bt.add_leaf(task) != eng::ai::BehaviorTree<2>::no_node, "bt: cabe la primera hoja");
	check(bt.add_leaf(task) != eng::ai::BehaviorTree<2>::no_node, "bt: cabe la segunda hoja");
	check(bt.add_leaf(task) == eng::ai::BehaviorTree<2>::no_node, "bt: la tercera no cabe");
}

} // namespace

int main() {
	std::printf("BehaviorTree:\n");
	test_guard_tree();
	test_composites();
	test_capacity();

	if (g_fail == 0u) {
		std::printf("OK: BehaviorTree (secuencia, selector, cortocircuito, capacidad)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
