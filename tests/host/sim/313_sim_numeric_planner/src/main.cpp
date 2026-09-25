// ============================================================================
// Test HOST-313: GOAP numerico en el ecosistema (`eng::sim`, dominio por plantilla)
// ============================================================================
//
// Valida que la capa de planificacion de `eng::sim` es generica sobre el dominio GOAP:
// `PlannerDriver` y `SimWorld` aceptan, como parametro de plantilla, tanto el dominio
// booleano ligero por defecto (`SimGoap`, 0 variables) como el numerico
// (`SimNumericGoap<N>`, hechos + `N` magnitudes). No reimplementa el algoritmo: el
// planificador sigue siendo `eng::ai::Goap`/`eng::ai::NumericGoap`.
//
//   1) `PlannerDriver<..., SimNumericGoap<2>>`: plan con precondiciones/efectos numericos
//      (forrajear -> comer baja el hambre), coste optimo, plan vacio si el objetivo ya se
//      cumple y fallo controlado si no hay solucion.
//   2) `SimWorld<..., SimNumericGoap<2>>`: el mundo propaga el dominio por plantilla y
//      `replan`/`current_action` funcionan con estado y acciones numericas.
//
// El caso booleano por defecto no cambia (lo cubre HOST-155).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/313_sim_numeric_planner

#include <cstdio>

#include <eng/sim/planner.hpp>
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

// Dominio de ejemplo: var 0 = hambre, var 1 = energia. `forage` consigue comida (hecho 0);
// `eat` exige comida y baja el hambre; `rest` recupera energia (irrelevante aqui).
enum : eng::u16 { kHasFood = 0u };
using Ai = SimNumericGoap<2u>;

eng::util::Array<Ai::Action, 3> needs_domain() {
	return {{
	    Ai::Builder {}.named("forage").produce(kHasFood).cost(1).build(),
	    Ai::Builder {}.named("eat").require(kHasFood).add(0u, -5).cost(1).build(),
	    Ai::Builder {}.named("rest").add(1u, 4).cost(1).build(),
	}};
}

void test_numeric_driver() {
	const auto acts = needs_domain();

	Ai::State start {};
	start.set_var(0u, 6u); // hambre 6
	Ai::Goal goal {};
	goal.var_le[0u] = 1u; // hambre <= 1

	PlannerDriver<64, 8, SimNumericGoap<2u>> driver;
	check(driver.replan(start, goal, acts.span()), "driver numerico: plan encontrado");
	check(driver.has_plan() && !driver.done(), "driver numerico: plan activo");
	check(driver.current() == 0u, "driver numerico: primero forrajear");
	check(driver.plan_cost() == 2u, "driver numerico: coste 2");
	driver.advance();
	check(driver.current() == 1u, "driver numerico: luego comer");
	driver.advance();
	check(driver.done() && !driver.has_plan(), "driver numerico: plan consumido");

	// Objetivo ya cumplido: plan vacio valido.
	Ai::State fed {};
	fed.set_var(0u, 1u);
	check(driver.replan(fed, goal, acts.span()), "driver numerico: objetivo ya cumplido");
	check(driver.done(), "driver numerico: plan vacio");

	// El hambre no sube con ninguna accion del dominio: no hay solucion.
	Ai::Goal impossible {};
	impossible.var_ge[0u] = 9u;
	check(!driver.replan(start, impossible, acts.span()), "driver numerico: sin solucion");
	check(driver.done(), "driver numerico: tras fallar no queda plan");
}

void test_budget_partial() {
	// Cadena numerica: subir el nivel 0 de 0 a 10 exige 10 acciones iguales.
	const eng::util::Array<Ai::Action, 1> acts {
	    {Ai::Builder {}.named("inc").add(0u, 1).cost(1).build()}};
	Ai::State start {};
	Ai::Goal goal {};
	goal.var_ge[0u] = 10u;

	Ai::Planner<64> full_planner;
	eng::u16 full[16] {};
	const eng::usize nf =
	    full_planner.plan(start, goal, acts.span(), eng::Span<eng::u16> {full, 16u});
	check(nf == 10u && full_planner.found() && !full_planner.partial(),
	      "presupuesto: sin limite plan completo de 10");

	Ai::Planner<64> planner;
	planner.set_budget(2u);
	eng::u16 plan[16] {};
	const eng::usize n = planner.plan(start, goal, acts.span(), eng::Span<eng::u16> {plan, 16u});
	check(!planner.found() && planner.partial(), "presupuesto: devuelve parcial");
	check(n >= 1u && n < 10u, "presupuesto: parcial mas corto que el plan");
	check(planner.expansions() <= 2u, "presupuesto: respeta el limite");

	// A traves del driver del ecosistema.
	PlannerDriver<64, 8, SimNumericGoap<2u>> driver;
	driver.set_budget(2u);
	check(driver.replan(start, goal, acts.span()), "driver con presupuesto: hay plan parcial");
	check(driver.partial() && driver.has_plan(), "driver con presupuesto: parcial activo");
}

void test_world_propagates_domain() {
	// El mundo hereda el dominio por el ultimo parametro de plantilla.
	SimWorld<SimTraits, 8, 4, 4, 8, 8, 64, SimNumericGoap<2u>> w;
	const EntityId id = w.spawn(1u, 0u, 0u, 0, 0);

	const auto acts = needs_domain();
	Ai::State start {};
	start.set_var(0u, 6u);
	Ai::Goal goal {};
	goal.var_le[0u] = 1u;

	check(w.replan(id, start, goal, acts.span()), "mundo numerico: plan encontrado");
	check(w.has_plan(id), "mundo numerico: plan activo");
	check(w.current_action(id) == 0u, "mundo numerico: primera accion");
	w.advance_plan(id);
	check(w.current_action(id) == 1u, "mundo numerico: segunda accion");
	w.abort_plan(id);
	check(!w.has_plan(id), "mundo numerico: abortado");
}

void test_decide_plan() {
	SimWorld<SimTraits, 8, 4, 4, 8, 8, 64, SimNumericGoap<2u>> w;
	const EntityId id = w.spawn(1u, 0u, 0u, 0, 0);

	check(!w.decide_plan(no_entity, 0u), "decide_plan: entidad invalida no planifica");

	// La decision del mundo usa la histeresis sobre la mente de la criatura: con la mente
	// recien creada coincide con `should_replan` sobre una copia de su estado.
	const auto& c = w.creature(0u);
	PlanState st = c.mind.plan;
	const bool expected = should_replan(c.personality, st, 7u, PlanParams {});
	check(w.decide_plan(id, 7u) == expected, "decide_plan: coincide con should_replan");
}

} // namespace

int main() {
	std::printf("Sim numeric planner:\n");
	test_numeric_driver();
	test_budget_partial();
	test_world_propagates_domain();
	test_decide_plan();

	if (g_fail == 0u) {
		std::printf("OK: Sim numeric planner (dominio GOAP por plantilla, driver y mundo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
