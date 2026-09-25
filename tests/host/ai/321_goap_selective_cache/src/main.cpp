// ============================================================================
// Test HOST-321: invalidacion selectiva de la cache de planes (`invalidate_selective`)
// ============================================================================
//
// La cache de `plan_cached` guarda dos planes distintos; `invalidate_selective(changed)`
// descarta **solo** las entradas cuyo plan depende (precondicion/efecto) de algun hecho
// cambiado, en vez de vaciar toda la cache. Se valida:
//
//   1) la cache acierta (no vuelve a expandir) en una consulta repetida;
//   2) un hecho que el plan **no** usa no lo invalida (sigue siendo acierto);
//   3) un hecho que el plan **usa** lo invalida (la consulta vuelve a planificar);
//   4) con dos entradas, se conserva la no afectada y su plan se **compacta** bien.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/ai/315_goap_selective_cache

#include <cstdio>

#include <eng/ai/planning/goap.hpp>
#include <eng/core/util/array.hpp>

namespace {

using Ai = eng::ai::Goap<>;
using Fact = eng::ai::Fact;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

// Cadena `base -> base+1 -> base+2 -> base+3` (3 acciones).
eng::util::Array<Ai::Action, 3> make_chain(eng::u16 base) {
	eng::util::Array<Ai::Action, 3> acts {};
	for (eng::usize i = 0u; i < 3u; ++i) {
		acts[i] = Ai::Builder {}
			      .require(static_cast<Fact>(base + i))
			      .produce(static_cast<Fact>(base + i + 1u))
			      .build();
	}
	return acts;
}

Ai::Goal goal_of(Fact target) {
	Ai::Goal g {};
	g.want_true.facts.set(target);
	return g;
}

Ai::State state_of(Fact only) {
	Ai::State s {};
	s.facts.set(only);
	return s;
}

void test_single_entry() {
	const auto acts = make_chain(0u);
	const Ai::Goal g = goal_of(3u);
	const Ai::State start = state_of(0u);

	Ai::Planner<64> planner;
	eng::u16 plan[8] {};
	const eng::usize n1 = planner.plan_cached(start, g, acts.span(), eng::Span<eng::u16> {plan, 8u});
	check(n1 == 3u, "primera consulta: 3 pasos");

	(void)planner.plan_cached(start, g, acts.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "cache: la consulta repetida no expande");

	// Hecho 9 no aparece en el plan: no lo invalida.
	planner.invalidate_selective(state_of(9u));
	(void)planner.plan_cached(start, g, acts.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "invalidacion irrelevante: sigue habiendo acierto");

	// Hecho 2 es efecto/precondicion del plan: lo invalida.
	planner.invalidate_selective(state_of(2u));
	const eng::usize n2 = planner.plan_cached(start, g, acts.span(), eng::Span<eng::u16> {plan, 8u});
	check(n2 == 3u && planner.expansions() > 0u, "invalidacion relevante: vuelve a planificar");
}

void test_compaction_keeps_other_entry() {
	const auto acts_a = make_chain(0u); // 0->1->2->3
	const auto acts_b = make_chain(4u); // 4->5->6->7
	const Ai::Goal ga = goal_of(3u);
	const Ai::Goal gb = goal_of(7u);
	const Ai::State sa = state_of(0u);
	const Ai::State sb = state_of(4u);

	Ai::Planner<64> planner;
	eng::u16 plan[8] {};
	check(planner.plan_cached(sa, ga, acts_a.span(), eng::Span<eng::u16> {plan, 8u}) == 3u,
	      "A: plan cacheado");
	check(planner.plan_cached(sb, gb, acts_b.span(), eng::Span<eng::u16> {plan, 8u}) == 3u,
	      "B: plan cacheado");

	// El hecho 2 solo lo usa A: B debe sobrevivir y seguir acertando tras compactar.
	planner.invalidate_selective(state_of(2u));
	(void)planner.plan_cached(sb, gb, acts_b.span(), eng::Span<eng::u16> {plan, 8u});
	check(planner.expansions() == 0u, "compactacion: B conserva su acierto");
	check(plan[0] == 0u && plan[1] == 1u && plan[2] == 2u, "compactacion: plan de B intacto");

	// A fue descartado: vuelve a planificar.
	const eng::usize na = planner.plan_cached(sa, ga, acts_a.span(), eng::Span<eng::u16> {plan, 8u});
	check(na == 3u && planner.expansions() > 0u, "compactacion: A se replanifica");
}

} // namespace

int main() {
	std::printf("GOAP selective cache:\n");
	test_single_entry();
	test_compaction_keeps_other_entry();

	if (g_fail == 0u) {
		std::printf("OK: GOAP invalidacion selectiva (dependencias, cache y compactacion)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
