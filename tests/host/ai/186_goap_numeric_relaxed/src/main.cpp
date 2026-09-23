// ============================================================================
// Test HOST-186: GOAP numerico con heuristica relajada y memo (fase B)
// ============================================================================
//
// TUTORIAL. Sobre el GOAP numerico (HOST-185) se añade una **heuristica de grafo
// relajado**: h_max sobre los hechos (ignorando los efectos de borrado) mas una cota
// numerica por el mayor delta por accion. Los valores `h` se **memoizan por estado
// entre llamadas** (caché de busquedas previas), de modo que replanificar con el
// mismo dominio reutiliza trabajo.
//
// Se valida que:
//   * `plan_relaxed` encuentra un plan valido que cumple el objetivo;
//   * el memo de heuristica se reutiliza entre dos consultas con el mismo dominio;
//   * `clear_heuristic_cache` lo vacia.
//
// Se ejecuta con:
//   bash tools/run-host-tests.sh tests/host/ai/186_goap_numeric_relaxed

#include <cstdio>

#include <eng/ai/planning/numeric_goap.hpp>

namespace {

using Ai = eng::ai::NumericGoap<4u>;
using State = Ai::State;
using Action = Ai::Action;
using Goal = Ai::Goal;

enum : eng::u16 { kHasMap = 0u };

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

void test_relaxed_plan_is_valid() {
	const auto actions = make_actions();
	Ai::Planner<256> planner;
	State start {};
	Goal goal {};
	goal.var_ge[1u] = 2u;
	goal.want_true.set(kHasMap);

	eng::u16 plan[16] {};
	const eng::usize n =
	    planner.plan_relaxed(start, goal, actions.span(), eng::Span<eng::u16> {plan, 16u});
	check(planner.found() && n > 0u, "relajada: encuentra plan");
	State s = start;
	for (eng::usize i = 0u; i < n; ++i) {
		eng::ai::apply(s, actions[plan[i]]);
	}
	check(eng::ai::satisfies(s, goal), "relajada: el plan cumple el objetivo");
}

void test_heuristic_memo() {
	const auto actions = make_actions();
	Ai::Planner<256> planner;
	Goal goal {};
	goal.var_ge[1u] = 2u;

	eng::u16 plan[16] {};
	State a {};
	(void)planner.plan_relaxed(a, goal, actions.span(), eng::Span<eng::u16> {plan, 16u});
	const eng::usize first_hits = planner.heuristic_hits();

	// Repite la misma consulta: el estado raiz (y los vistos) ya estan en el memo.
	(void)planner.plan_relaxed(a, goal, actions.span(), eng::Span<eng::u16> {plan, 16u});
	check(planner.heuristic_hits() > first_hits, "memo: la segunda consulta acierta");

	planner.clear_heuristic_cache();
	check(planner.heuristic_hits() == 0u, "memo: clear_heuristic_cache lo vacia");
}

} // namespace

int main() {
	std::printf("GOAP numerico relajado:\n");
	test_relaxed_plan_is_valid();
	test_heuristic_memo();

	if (g_fail == 0u) {
		std::printf("OK: GOAP numerico con h relajada y memo de heuristica\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
