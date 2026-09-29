// ============================================================================
// Test HOST-320: GOAP con presupuesto *anytime* (`set_budget`/`partial`)
// ============================================================================
//
// Valida la busqueda acotada de `eng::ai::Goap`: con `set_budget(n)` el planner expande
// como maximo `n` nodos y, si no alcanza el objetivo, devuelve el mejor **parcial**
// (menor `h`; a igual `h`, el de mayor avance `g`). El parcial no se cachea y no es una
// solucion: el llamador decide si lo ejecuta o espera mas presupuesto. Pensado para
// repartir CPU por tick en el 68000.
//
//   1) sin presupuesto: plan completo de 7 pasos y `partial()` falso;
//   2) con presupuesto corto (2): parcial que es prefijo del plan completo, no alcanza el
//      objetivo y respeta el limite de expansiones;
//   3) con presupuesto amplio: de nuevo el plan completo (no parcial).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/314_goap_anytime

#include <cstdio>

#include <eng/ai/planning/goap.hpp>
#include <eng/core/util/array.hpp>

namespace {

using Ai = eng::ai::Goap<>;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// Cadena 0->1->...->7: cada accion exige el eslabon i y produce i+1.
eng::util::Array<Ai::Action, 7> chain() {
	eng::util::Array<Ai::Action, 7> acts {};
	for (eng::usize i = 0u; i < 7u; ++i) {
		acts[i] = Ai::Builder {}
			      .require(static_cast<eng::ai::Fact>(i))
			      .produce(static_cast<eng::ai::Fact>(i + 1u))
			      .build();
	}
	return acts;
}

Ai::Goal make_goal() {
	Ai::Goal g {};
	g.want_true.facts.set(static_cast<eng::ai::Fact>(7u));
	return g;
}

Ai::State make_start() {
	Ai::State s {};
	s.facts.set(static_cast<eng::ai::Fact>(0u));
	return s;
}

void test_full_plan() {
	const auto acts = chain();
	Ai::Planner<64> planner;
	eng::u16 plan[16] {};
	const eng::usize n =
	    planner.plan(make_start(), make_goal(), acts.span(), eng::Span<eng::u16> {plan, 16u});
	check(planner.found() && !planner.partial(), "sin presupuesto: plan completo");
	check(n == 7u, "sin presupuesto: 7 pasos");
	check(planner.budget() == 0u, "sin presupuesto: budget 0 (sin limite)");
}

void test_budgeted_partial() {
	const auto acts = chain();

	eng::u16 full[16] {};
	Ai::Planner<64> ref;
	const eng::usize nf =
	    ref.plan(make_start(), make_goal(), acts.span(), eng::Span<eng::u16> {full, 16u});
	check(nf == 7u, "referencia: plan completo de 7 pasos");

	Ai::Planner<64> planner;
	planner.set_budget(2u);
	eng::u16 plan[16] {};
	const eng::usize n =
	    planner.plan(make_start(), make_goal(), acts.span(), eng::Span<eng::u16> {plan, 16u});
	check(!planner.found(), "presupuesto 2: no alcanza el objetivo");
	check(planner.partial(), "presupuesto 2: devuelve parcial");
	check(n >= 1u && n < 7u, "presupuesto 2: parcial mas corto que el plan");
	check(planner.expansions() <= 2u, "presupuesto 2: respeta el limite de expansiones");

	bool prefix = n <= nf;
	for (eng::usize i = 0u; prefix && i < n; ++i) {
		prefix = plan[i] == full[i];
	}
	check(prefix, "presupuesto 2: el parcial es prefijo del plan completo");
}

void test_unreachable_without_budget() {
	// Sin presupuesto y sin solucion: 0 acciones y `partial()` falso. El parcial solo
	// aplica si el presupuesto **corta** la busqueda, no si el espacio se agota.
	const eng::util::Array<Ai::Action, 1> acts {
	    {Ai::Builder {}.require(0u).produce(1u).build()}};
	Ai::State start {};
	start.facts.set(static_cast<eng::ai::Fact>(5u)); // hecho aislado: ninguna accion aplica
	Ai::Goal g {};
	g.want_true.facts.set(static_cast<eng::ai::Fact>(6u));
	Ai::Planner<64> planner;
	eng::u16 plan[8] {};
	const eng::usize n = planner.plan(start, g, acts.span(), eng::Span<eng::u16> {plan, 8u});
	check(n == 0u && !planner.found() && !planner.partial(),
	      "sin solucion: 0 acciones y sin parcial");
}

void test_wide_budget_is_complete() {
	const auto acts = chain();
	Ai::Planner<64> planner;
	planner.set_budget(1000u);
	eng::u16 plan[16] {};
	const eng::usize n =
	    planner.plan(make_start(), make_goal(), acts.span(), eng::Span<eng::u16> {plan, 16u});
	check(planner.found() && !planner.partial(), "presupuesto amplio: completa");
	check(n == 7u, "presupuesto amplio: 7 pasos");
}

} // namespace

int main() {
	std::printf("GOAP anytime:\n");
	test_full_plan();
	test_budgeted_partial();
	test_unreachable_without_budget();
	test_wide_budget_is_complete();

	if (g_fail == 0u) {
		std::printf("OK: GOAP con presupuesto (parcial vs. completo, prefijo y limite)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
