// ============================================================================
// Test HOST-155: planificacion GOAP para criaturas (`eng::sim::PlannerDriver`)
// ============================================================================
//
// Valida `eng/sim/planner.hpp`, la capa que conecta el planificador genérico
// `eng::ai::Goap` con el modelo de criatura:
//
//   1) `plans`: que la decision de planificar salga de la personalidad (curiosidad y
//      autonomia) con umbrales parametricos.
//   2) `PlanRunner`: cargar un plan, consumir pasos por tick, terminar y abortar.
//   3) `PlannerDriver`: replanificar (plan optimo de 3 pasos), plan vacio cuando el
//      objetivo ya se cumple, y fallo controlado cuando no hay solucion.
//
// El dominio (hechos y acciones) es de ejemplo; el engine solo aporta el conductor.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/155_sim_planner

#include <cstdio>

#include <eng/sim/domain.hpp>
#include <eng/sim/planner.hpp>

namespace {

using namespace eng::sim;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

void test_plan_decision() {
	Personality planner {};
	planner.curiosity = 80u;
	planner.autonomy = 60u;
	check(plans(planner), "plans: criatura curiosa y autonoma planifica");

	Personality curious_only {};
	curious_only.curiosity = 80u;
	curious_only.autonomy = 20u;
	check(!plans(curious_only), "plans: sin autonomia no planifica");

	Personality dull {};
	dull.curiosity = 20u;
	dull.autonomy = 90u;
	check(!plans(dull), "plans: sin curiosidad no planifica");
}

void test_plan_runner() {
	PlanRunner<4> runner;
	check(runner.done() && !runner.has_next(), "runner: arranca terminado");

	const eng::u16 steps[2] = {5u, 9u};
	check(runner.load(eng::Span<const eng::u16> {steps, 2}), "runner: carga el plan");
	check(runner.has_next() && runner.current() == 5u, "runner: primer paso");
	runner.advance();
	check(runner.current() == 9u, "runner: segundo paso");
	runner.advance();
	check(runner.done() && !runner.has_next(), "runner: termina al agotar los pasos");

	// Un plan que no cabe se rechaza.
	const eng::u16 big[5] = {1u, 2u, 3u, 4u, 5u};
	check(!runner.load(eng::Span<const eng::u16> {big, 5}), "runner: plan que no cabe");

	runner.load(eng::Span<const eng::u16> {steps, 2});
	runner.abort();
	check(runner.done(), "runner: abortar");
}

void test_driver() {
	using Ai = SimGoap;
	constexpr eng::util::Array<Ai::Action, 3> acts { {
		Ai::Builder {}.require(0u).produce(1u).build(),
		Ai::Builder {}.require(1u).produce(2u).build(),
		Ai::Builder {}.require(2u).produce(3u).build(),
	} };

	Ai::Goal goal {};
	goal.want_true.facts.set(static_cast<eng::ai::Fact>(3u));

	// Plan optimo: 0 -> 1 -> 2 (tres acciones encadenadas).
	Ai::State start {};
	start.facts.set(static_cast<eng::ai::Fact>(0u));
	PlannerDriver<64, 8> driver;
	check(driver.replan(start, goal, acts.span()), "driver: encuentra plan");
	check(driver.has_plan() && !driver.done(), "driver: plan activo");
	check(driver.current() == 0u, "driver: primer paso es la accion 0");
	check(driver.plan_cost() == 3u, "driver: coste optimo 3");
	check(driver.expansions() > 0u, "driver: se expandieron nodos");

	driver.advance();
	check(driver.current() == 1u, "driver: segundo paso");
	driver.advance();
	check(driver.current() == 2u, "driver: tercer paso");
	driver.advance();
	check(driver.done() && !driver.has_plan(), "driver: plan consumido");

	// Objetivo ya cumplido: plan vacio valido.
	Ai::State done_start {};
	done_start.facts.set(static_cast<eng::ai::Fact>(3u));
	check(driver.replan(done_start, goal, acts.span()), "driver: objetivo ya cumplido");
	check(driver.done() && !driver.has_plan(), "driver: plan vacio");

	// Sin solucion: el estado inicial no tiene el hecho 0 y no hay accion que lo cree.
	Ai::State empty {};
	check(!driver.replan(empty, goal, acts.span()), "driver: sin solucion devuelve false");
	check(driver.done(), "driver: tras fallar no queda plan");
}

void test_construction_domain() {
	using Ai = SimGoap;
	const auto acts = ConstructionDomain::actions();
	PlannerDriver<64, 8> driver;

	// Objetivo refugio desde cero: recoger materiales -> fabricar -> recoger -> construir.
	const Ai::Goal shelter = ConstructionDomain::goal(false, true);
	check(driver.replan(start_state(SimInventory {}), shelter, acts.span()),
	      "dominio: plan de refugio encontrado");
	check(action_kind_of(driver.current()) == SimActionKind::Gather,
	      "dominio: empieza recogiendo materiales");
	check(driver.plan_cost() == 4u,
	      "dominio: coste 4 (recoger, fabricar, recoger, construir)");
	for (int i = 0; i < 4; ++i) {
		driver.advance();
	}
	check(driver.done(), "dominio: plan de refugio consumido");

	// Objetivo comer desde cero: forrajear -> comer.
	const Ai::Goal fed = ConstructionDomain::goal(true, false);
	check(driver.replan(start_state(SimInventory {}), fed, acts.span()),
	      "dominio: plan de comida encontrado");
	check(action_kind_of(driver.current()) == SimActionKind::Forage,
	      "dominio: empieza forrajeando");
	driver.advance();
	check(action_kind_of(driver.current()) == SimActionKind::Eat, "dominio: luego come");

	// Si ya tiene materiales y herramienta, solo construye (un paso).
	SimInventory ready {};
	ready.has_materials = true;
	ready.has_tool = true;
	check(driver.replan(start_state(ready), shelter, acts.span()),
	      "dominio: plan con inventario previo");
	check(action_kind_of(driver.current()) == SimActionKind::Build,
	      "dominio: con materiales y herramienta solo construye");
	check(driver.plan_cost() == 1u, "dominio: coste de un paso");

	check(action_kind_of(99u) == SimActionKind::Count, "dominio: accion fuera de rango");
	check(action_name(SimActionKind::CraftTool)[0] == 'c', "dominio: nombre legible");
}

} // namespace

int main() {
	std::printf("Sim planner:\n");
	test_plan_decision();
	test_plan_runner();
	test_driver();
	test_construction_domain();

	if (g_fail == 0u) {
		std::printf("OK: Sim planner (decision, PlanRunner, PlannerDriver GOAP, dominio de "
			    "construccion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
