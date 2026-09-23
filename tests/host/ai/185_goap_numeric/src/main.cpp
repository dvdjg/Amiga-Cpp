// ============================================================================
// Test HOST-185: GOAP numerico cuantizado (enteros, decimales y cache)
// ============================================================================
//
// TUTORIAL. `eng::ai::NumericGoap` extiende el GOAP booleano con hasta 4 variables
// de nivel (u8): los enteros son niveles directos y los decimales, niveles escalados
// (p. ej. escala 4 -> 0.25 por nivel). La clave sigue siendo exacta (32 bits de
// hechos + 32 bits de niveles) y el planner es A* como el booleano.
//
// Se validan:
//   * plan con precondiciones/efectos numericos (juntar madera, fabricar y minar);
//   * decimales por escala (aceite 0.25);
//   * saturacion de niveles (no desbordan 0..255);
//   * cache de planes (`plan_cached`, repite sin buscar);
//   * reutilizacion del sufijo (`plan_reusing`, tras ejecutar un paso).
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/ai/185_goap_numeric

#include <cstdio>

#include <eng/ai/planning/numeric_goap.hpp>

namespace {

using Ai = eng::ai::NumericGoap<4u>; // vars: 0=madera, 1=oro, 2=herramienta, 3=aceite
using State = Ai::State;
using Action = Ai::Action;
using Goal = Ai::Goal;

enum : eng::u16 { kHasMap = 0u, kFed = 1u };

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

eng::util::Array<Action, 5> make_actions() {
	return {{
	    Ai::Builder {}.named("chop").add(0u, 1).cost(1).build(),
	    Ai::Builder {}.named("craft").var_ge(0u, 2).set_var(0u, 0u).add(2u, 1).cost(2).build(),
	    Ai::Builder {}.named("mine").var_ge(2u, 1).add(1u, 1).cost(1).build(),
	    Ai::Builder {}.named("refine").add(3u, 4).cost(1).build(),
	    Ai::Builder {}.named("find_map").produce(kHasMap).cost(1).build(),
	}};
}

void test_numeric_plan() {
	const auto actions = make_actions();
	Ai::Planner<256> planner;

	State start {};
	Goal goal {};
	goal.var_ge[1u] = 2u;          // oro >= 2
	goal.want_true.set(kHasMap);   // y tener el mapa

	eng::u16 plan[16] {};
	const eng::usize n = planner.plan(start, goal, actions.span(), eng::Span<eng::u16> {plan, 16u});
	check(planner.found(), "plan: encontrado");
	check(n == 6u, "plan: 6 acciones (chop,chop,craft,mine,mine + find_map)");
	check(planner.plan_cost() == 7u, "plan: coste 7");

	// Ejecuta el plan y comprueba el objetivo.
	State s = start;
	for (eng::usize i = 0u; i < n; ++i) {
		eng::ai::apply(s, actions[plan[i]]);
	}
	check(eng::ai::satisfies(s, goal), "plan: al ejecutarlo se cumple el objetivo");
	check(s.vars[1u] == 2u && s.facts.test(kHasMap), "plan: oro=2 y mapa");
}

void test_decimals_by_scale() {
	const auto actions = make_actions();
	Ai::Planner<64> planner;
	State start {};
	Goal goal {};
	goal.var_ge[3u] = 4u; // aceite 1.0 con escala 4 (nivel 4)
	eng::u16 plan[4] {};
	const eng::usize n = planner.plan(start, goal, actions.span(), eng::Span<eng::u16> {plan, 4u});
	check(planner.found() && n == 1u, "decimales: una accion refine");
	check(actions[plan[0]].name != nullptr && plan[0] == 3u, "decimales: es refine");
}

void test_saturation_and_key() {
	State s {};
	s.set_var(0u, 255u);
	Action chop = Ai::Builder {}.add(0u, 10).build();
	eng::ai::apply(s, chop);
	check(s.vars[0u] == 255u, "saturacion: no desborda 255");

	s.set_var(2u, 5u);
	Action sub = Ai::Builder {}.add(2u, -20).build();
	eng::ai::apply(s, sub);
	check(s.vars[2u] == 0u, "saturacion: no baja de 0");

	State a {};
	a.set_var(1u, 3u);
	a.set_var(3u, 7u);
	const State b = State::from_key(a.key());
	check(b.vars[1u] == 3u && b.vars[3u] == 7u, "clave: round-trip de niveles");
}

void test_plan_cache() {
	const auto actions = make_actions();
	Ai::Planner<256, 4u, 64u> planner;
	State start {};
	Goal goal {};
	goal.var_ge[1u] = 1u;

	eng::u16 plan1[8] {};
	const eng::usize n1 = planner.plan_cached(start, goal, actions.span(), eng::Span<eng::u16> {plan1, 8u});
	check(n1 > 0u && planner.expansions() > 0u, "cache: primera busqueda expande");
	eng::u16 plan2[8] {};
	const eng::usize n2 = planner.plan_cached(start, goal, actions.span(), eng::Span<eng::u16> {plan2, 8u});
	check(n2 == n1 && planner.expansions() == 0u, "cache: la segunda no busca");
	check(plan1[0] == plan2[0], "cache: mismo plan");
}

void test_suffix_reuse() {
	const auto actions = make_actions();
	Ai::Planner<256> planner;
	State start {};
	Goal goal {};
	goal.var_ge[1u] = 2u;
	goal.want_true.set(kHasMap);

	eng::u16 plan[16] {};
	const eng::usize n = planner.plan(start, goal, actions.span(), eng::Span<eng::u16> {plan, 16u});
	check(n >= 2u, "sufijo: plan de varios pasos");

	// Ejecuta el primer paso y reutiliza el sufijo desde el nuevo estado.
	State after = start;
	eng::ai::apply(after, actions[plan[0]]);
	eng::u16 reused[16] {};
	const eng::usize r = planner.plan_reusing(after, goal, actions.span(),
						  eng::Span<eng::u16> {reused, 16u},
						  eng::Span<const eng::u16> {plan, n}, 0u);
	check(r == n - 1u, "sufijo: reutiliza n-1 pasos");
	check(planner.expansions() == 0u, "sufijo: sin abrir busqueda");
}

} // namespace

int main() {
	std::printf("GOAP numerico:\n");
	test_numeric_plan();
	test_decimals_by_scale();
	test_saturation_and_key();
	test_plan_cache();
	test_suffix_reuse();

	if (g_fail == 0u) {
		std::printf("OK: GOAP numerico (enteros, decimales, saturacion, memo y sufijo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
