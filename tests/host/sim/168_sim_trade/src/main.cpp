// ============================================================================
// Test HOST-168: trueque y regateo (economia + sociedad)
// ============================================================================
//
// Valida `eng/sim/economy.hpp` (trueque) y su uso en `SimWorld`:
//
//   1) `trade_value`: balance de valor del intercambio.
//   2) `bargain_score`: sube con el balance de valor y con la necesidad de lo recibido.
//   3) `execute_trade`: intercambia materiales y sube la demanda; falla si falta algo.
//   4) `SimWorld::offer_trade`: cierra el trato entre criaturas y sube su reputacion.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/168_sim_trade

#include <cstdio>

#include <eng/sim/economy.hpp>
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

void test_value_and_bargain() {
	Economy eco;
	eco.reset();
	const TradeOffer fair {ItemKind::Food, 1u, ItemKind::Tool, 1u}; // 8 - 5 = +3
	const TradeOffer bad {ItemKind::Tool, 1u, ItemKind::Food, 1u};  // 5 - 8 = -3
	check(trade_value(eco, fair) == 3 && trade_value(eco, bad) == -3,
	      "trueque: balance de valor");

	const Score keen = bargain_score(eco, fair, 200u);
	const Score dull = bargain_score(eco, bad, 0u);
	check(keen > dull, "regateo: valor y necesidad suben la disposicion");
	check(bargain_score(eco, fair, 255u) >= bargain_score(eco, fair, 0u),
	      "regateo: mas necesidad no baja la puntuacion");
}

void test_execute() {
	Economy eco;
	eco.reset();
	Inventory a;
	Inventory b;
	a.add(ItemKind::Food, 2u);
	b.add(ItemKind::Tool, 1u);
	const TradeOffer o {ItemKind::Food, 2u, ItemKind::Tool, 1u};

	check(execute_trade(a, b, eco, o), "trueque: se cierra el intercambio");
	check(a.count(ItemKind::Food) == 0u && a.count(ItemKind::Tool) == 1u,
	      "trueque: el comprador entrega y recibe");
	check(b.count(ItemKind::Tool) == 0u && b.count(ItemKind::Food) == 2u,
	      "trueque: el vendedor entrega y recibe");
	check(eco.demand_of(ItemKind::Tool) == 1u && eco.demand_of(ItemKind::Food) == 2u,
	      "trueque: lo intercambiado sube su demanda");

	check(!execute_trade(a, b, eco, o), "trueque: sin existencias no hay trato");
}

void test_world_trade() {
	SimWorld<SimTraits, 8, 4, 4, 8> w;
	w.economy().reset();
	const EntityId x = w.spawn(1u, 0u, 0u, 0, 0);
	const EntityId y = w.spawn(1u, 1u, 0u, 1, 0);
	w.find(x)->carrying.add(ItemKind::Food, 5u);
	w.find(y)->carrying.add(ItemKind::Material, 3u);

	const TradeOffer o {ItemKind::Food, 2u, ItemKind::Material, 1u};
	const bool ok = w.offer_trade(x, y, o);
	check(ok, "world: el trato se cierra");
	check(w.find(x)->carrying.count(ItemKind::Food) == 3u &&
		      w.find(x)->carrying.count(ItemKind::Material) == 1u,
	      "world: inventario del primero actualizado");
	check(w.find(y)->carrying.count(ItemKind::Food) == 2u &&
		      w.find(y)->carrying.count(ItemKind::Material) == 2u,
	      "world: inventario del segundo actualizado");
	check(w.society().rep(0u) > 0 && w.society().rep(1u) > 0,
	      "world: comerciar mejora la reputacion de ambos");
	const TradeOffer bad {ItemKind::Tool, 1u, ItemKind::Food, 1u};
	check(!w.offer_trade(x, y, bad), "world: sin existencias no hay trato");
}

} // namespace

int main() {
	std::printf("Sim trade:\n");
	test_value_and_bargain();
	test_execute();
	test_world_trade();

	if (g_fail == 0u) {
		std::printf("OK: Sim trade (valor, regateo, intercambio, reputacion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
