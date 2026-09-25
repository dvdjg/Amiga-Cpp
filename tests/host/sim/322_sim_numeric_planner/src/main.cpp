// ============================================================================
// Test HOST-322: GOAP numerico en el ecosistema (`eng::sim`, dominio por plantilla)
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

#include <eng/sim/domain.hpp>
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

	// LOD: sin realized no se planifica; tras realizarla, si.
	check(!w.decide_plan(id, 0u), "decide_plan: LOD (abstracta) no planifica");
	w.realize_room(0u, 8u);
	check(!w.decide_plan(no_entity, 0u), "decide_plan: entidad invalida no planifica");

	// La decision del mundo usa la histeresis sobre la mente de la criatura: con la mente
	// recien creada coincide con `should_replan` sobre una copia de su estado.
	const auto& c = w.creature(0u);
	PlanState st = c.mind.plan;
	const bool expected = should_replan(c.personality, st, 7u, PlanParams {});
	check(w.decide_plan(id, 7u) == expected, "decide_plan: coincide con should_replan");
}

void test_plan_tick() {
	SimWorld<SimTraits, 8, 4, 4, 8, 8, 64, SimNumericGoap<2u>> w;
	const EntityId id = w.spawn(1u, 0u, 0u, 0, 0);
	// Criatura planificadora (curiosa y autonoma) y **realizada** (LOD).
	w.creature(0u).personality.curiosity = 80u;
	w.creature(0u).personality.autonomy = 60u;
	w.realize_room(0u, 8u);

	const auto acts = needs_domain();
	Ai::State start {};
	start.set_var(0u, 6u);
	Ai::Goal goal {};
	goal.var_le[0u] = 1u;
	PlanParams pp {};
	pp.budget = 4u;

	check(w.plan_tick(id, start, goal, acts.span(), 0u, pp), "plan_tick: planifica en el tick");
	check(w.has_plan(id), "plan_tick: deja plan activo");
	// En el mismo intervalo no replanifica, pero conserva el plan.
	check(w.plan_tick(id, start, goal, acts.span(), 1u, pp), "plan_tick: mantiene el plan");
	check(!w.plan_tick(no_entity, start, goal, acts.span(), 0u, pp),
	      "plan_tick: entidad invalida no planifica");
}

void test_plan_tick_reusing() {
	// Tras ejecutar un paso, el sufijo del plan sigue valiendo: `plan_tick` lo reutiliza.
	SimWorld<SimTraits, 8, 4, 4, 8, 8, 64> w;
	const EntityId id = w.spawn(1u, 0u, 0u, 0, 0);
	w.creature(0u).personality.curiosity = 200u;
	w.creature(0u).personality.autonomy = 200u;
	w.realize_room(0u, 8u);

	const auto acts = ConstructionDomain::actions();
	const SimGoap::Goal goal = ConstructionDomain::goal(false, true);
	PlanParams pp {};
	pp.budget = 64u;
	pp.replan_interval = 0u;

	check(w.plan_tick(id, start_state(SimInventory {}), goal, acts.span(), 0u, pp),
	      "reuse: plan inicial");
	check(w.planner_expansions() > 0u, "reuse: la primera vez busca");

	// Se ha ejecutado el primer paso (Gather): hay materiales y el sufijo ya aplica.
	SimInventory after_inv {};
	after_inv.has_materials = true;
	w.advance_plan(id);
	check(w.plan_tick(id, start_state(after_inv), goal, acts.span(), 1u, pp),
	      "reuse: replanifica tras el paso");
	check(w.planner_expansions() == 0u, "reuse: no vuelve a buscar (sufijo aplicable)");
}

void test_htn_driver() {
	// La criatura tambien puede planificar por **HTN** (descomposicion): el mundo guarda ese
	// plan y lo consume igual que uno del GOAP.
	SimWorld<SimTraits, 8, 4, 4, 8, 8, 64, SimNumericGoap<2u>> w;
	const EntityId id = w.spawn(1u, 0u, 0u, 0, 0);

	HtnDriver<32u, 5u, 1u, 4u, 8u> htn;
	htn.set_domain(build_shelter_htn());
	const auto acts = ConstructionDomain::actions();
	check(htn.replan(start_state(SimInventory {}), htn.domain().compound_at(0u), acts.span()),
	      "htn driver: descompone la tarea compuesta");
	check(w.store_plan(id, htn.runner()), "htn driver: el mundo guarda el plan");
	check(w.has_plan(id), "htn driver: plan activo");
	check(w.current_action(id) == static_cast<eng::u16>(SimActionKind::Gather),
	      "htn driver: primera accion");
	w.advance_plan(id);
	check(w.current_action(id) == static_cast<eng::u16>(SimActionKind::CraftTool),
	      "htn driver: segunda accion");

	// Tras ejecutar el primer paso (Gather) el sufijo sigue aplicando: no hay que
	// re-descomponer. Con el estado acorde (ya hay materiales).
	SimInventory after_inv {};
	after_inv.has_materials = true;
	check(htn.replan_reusing(start_state(after_inv), htn.domain().compound_at(0u),
				 acts.span(), 1u),
	      "htn driver: reutiliza el sufijo");
	check(htn.reused(), "htn driver: marca la reutilizacion");
}

void test_plan_selection() {
	// El mundo y el HTN comparten dominio (hechos booleanos), como exige `plan_for`.
	SimWorld<SimTraits, 8, 4, 4, 8, 8, 64> w;
	const EntityId id = w.spawn(1u, 0u, 0u, 0, 0);
	w.creature(0u).personality.curiosity = 80u;
	w.creature(0u).personality.autonomy = 60u;
	w.realize_room(0u, 8u); // LOD: realizada para que planifique

	HtnDriver<32u, 5u, 1u, 4u, 8u> htn;
	htn.set_domain(build_shelter_htn());
	const auto acts = ConstructionDomain::actions();
	const SimGoap::State start = start_state(SimInventory {});
	SimGoap::Goal goal {};
	goal.want_true.facts.set(static_cast<eng::ai::Fact>(SimFact::Fed));
	const eng::ai::HtnCompound root = htn.domain().compound_at(0u);

	// Objetivo suelto -> GOAP (el mundo decide y replanifica).
	check(plan_for(w, htn, PlanKind::Goap, id, start, goal, root, acts.span(), 0u,
		       PlanParams {}),
	      "seleccion: objetivo suelto -> GOAP");
	check(w.has_plan(id), "seleccion: GOAP deja plan");
	check(w.current_action(id) == static_cast<eng::u16>(SimActionKind::Forage),
	      "seleccion: GOAP empieza por forage");

	// Tarea compuesta -> HTN (descomposicion).
	w.abort_plan(id);
	check(plan_for(w, htn, PlanKind::Htn, id, start, goal, root, acts.span(), 0u,
		       PlanParams {}),
	      "seleccion: tarea compuesta -> HTN");
	check(w.current_action(id) == static_cast<eng::u16>(SimActionKind::Gather),
	      "seleccion: HTN empieza por gather");
}

} // namespace

int main() {
	std::printf("Sim numeric planner:\n");
	test_numeric_driver();
	test_budget_partial();
	test_world_propagates_domain();
	test_decide_plan();
	test_plan_tick();
	test_plan_tick_reusing();
	test_htn_driver();
	test_plan_selection();

	if (g_fail == 0u) {
		std::printf("OK: Sim numeric planner (dominio GOAP por plantilla, driver y mundo)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
