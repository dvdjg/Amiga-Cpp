// ============================================================================
// Test HOST-316: cache selectiva (hechos + variables) y LRU del GOAP numerico
// ============================================================================
//
// Sobre `eng::ai::NumericGoap`, la cache de `plan_cached` guarda las **dependencias** de
// cada plan (`used_facts`, `used_vars`) y las usa para dos cosas:
//
//   1) `invalidate_selective(changed)`: descarta solo las entradas que dependen de un hecho
//      o una variable cambiados, no toda la cache;
//   2) politica **LRU+menos-usos**: al llenarse, desaloja la entrada con menos `hits`.
//
// Se valida el caso positivo (se invalida/desaloja lo que toca) y el negativo (sobrevive
// lo que no toca).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/316_numeric_selective_cache

#include <cstdio>

#include <eng/ai/planning/numeric_goap.hpp>
#include <eng/core/util/array.hpp>

namespace {

using Ai = eng::ai::NumericGoap<2u>;
using Fact = eng::ai::Fact;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// Dominio de hechos: 0 -> 1 -> 2 (dos acciones).
eng::util::Array<Ai::Action, 2> facts_chain() {
	return {{Ai::Builder {}.require(static_cast<Fact>(0u)).produce(static_cast<Fact>(1u)).build(),
		 Ai::Builder {}.require(static_cast<Fact>(1u)).produce(static_cast<Fact>(2u)).build()}};
}

// Dominio de variable: sube la variable 1 de uno en uno.
eng::util::Array<Ai::Action, 1> var_inc() {
	return {{Ai::Builder {}.add(1u, 1).cost(1).build()}};
}

Ai::Goal goal_fact(Fact f) {
	Ai::Goal g {};
	g.want_true.set(f); // en el dominio numerico `want_true` es un `Facts`, no un `State`
	return g;
}
Ai::Goal goal_var_ge(eng::usize v, eng::u8 min) {
	Ai::Goal g {};
	g.var_ge[v] = min;
	return g;
}
Ai::State state_with_fact(Fact f) {
	Ai::State s {};
	s.facts.set(f);
	return s;
}

void test_invalidate_by_fact_and_var() {
	const auto fa = facts_chain();
	const auto fb = var_inc();
	const Ai::State sa = state_with_fact(0u); // hechos: 0 -> 2
	const Ai::State sb {};                    // sin hechos; sube la variable 1 a 3
	const Ai::Goal ga = goal_fact(2u);
	const Ai::Goal gb = goal_var_ge(1u, 3u);

	Ai::Planner<64> planner;
	eng::u16 plan[8] {};
	check(planner.plan_cached(sa, ga, fa.span(), eng::Span<eng::u16> {plan, 8u}) == 2u,
	      "A (hechos) cacheado");
	check(planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u}) == 3u,
	      "B (variable) cacheado");

	// Cambia el hecho 1: invalida A (lo usa) pero no B.
	planner.invalidate_selective(state_with_fact(1u));
	(void)planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "invalida por hecho: B sobrevive");
	(void)planner.plan_cached(sa, ga, fa.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() > 0u, "invalida por hecho: A se replanifica");

	// Cambia la variable 1: invalida B (la usa) pero no A.
	Ai::State changed_var {};
	changed_var.vars[1u] = 5u;
	planner.invalidate_selective(changed_var);
	(void)planner.plan_cached(sa, ga, fa.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "invalida por variable: A sobrevive");
	(void)planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() > 0u, "invalida por variable: B se replanifica");

	// Cambio irrelevante (hecho 9 y variable 0): no invalida nada.
	Ai::State irrelevant {};
	irrelevant.facts.set(9u);
	irrelevant.vars[0u] = 7u;
	planner.invalidate_selective(irrelevant);
	(void)planner.plan_cached(sa, ga, fa.span(), eng::Span<eng::u16> {plan, 8u});
	(void)planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "cambio irrelevante: siguen en cache");
}

void test_lru_eviction() {
	const auto fa = facts_chain();
	const auto fb = var_inc();
	const Ai::State sa = state_with_fact(0u);
	const Ai::State sb {};
	const Ai::Goal ga = goal_fact(2u);
	const Ai::Goal gb = goal_var_ge(1u, 3u);
	const Ai::Goal gc = goal_fact(1u); // distinto objetivo, resoluble desde sa

	Ai::Planner<64, 2u, 64u> planner; // MaxCachedPlans = 2
	eng::u16 plan[8] {};
	(void)planner.plan_cached(sa, ga, fa.span(), eng::Span<eng::u16> {plan, 8u});
	(void)planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u});
	(void)planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u}); // B: hits=1
	(void)planner.plan_cached(sa, gc, fa.span(), eng::Span<eng::u16> {plan, 8u}); // llena: cae A

	(void)planner.plan_cached(sb, gb, fb.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "LRU: la entrada mas usada (B) sobrevive");
	(void)planner.plan_cached(sa, ga, fa.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() > 0u, "LRU: la menos usada (A) fue desalojada");
}

} // namespace

int main() {
	std::printf("GOAP numeric selective cache:\n");
	test_invalidate_by_fact_and_var();
	test_lru_eviction();

	if (g_fail == 0u) {
		std::printf("OK: GOAP numerico (invalidacion selectiva por hecho/variable y LRU)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
