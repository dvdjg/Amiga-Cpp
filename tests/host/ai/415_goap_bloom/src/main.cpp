// HOST-415: el filtro Bloom omite búsquedas exactas negativas, nunca confirma presencia.

#if !defined(ENG_AMIGA)
#include <cstdio>
#endif

#include <eng/ai/planning/goap.hpp>
#include <eng/core/util/bloom.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/hash_map.hpp>

namespace {

using eng::u16;
using eng::usize;
using Ai = eng::ai::Goap<>;
using DefaultPlanner = Ai::Planner<64u>;
using SmallBloomPlanner = Ai::Planner<64u, 4u, 64u, 32u>;
using LargerBloomPlanner = Ai::Planner<64u, 4u, 64u, 256u>;
static_assert(sizeof(DefaultPlanner) < sizeof(SmallBloomPlanner),
	      "Bloom permanece ausente del planner por defecto");
static_assert(sizeof(SmallBloomPlanner) < sizeof(LargerBloomPlanner),
	      "el presupuesto Bloom mayor ocupa más memoria inline");

unsigned g_fail = 0u;
void check(bool ok, const char* what) {
	if (!ok) {
	#if !defined(ENG_AMIGA)
		std::printf("[FAIL] %s\n", what);
	#else
		(void)what;
	#endif
		++g_fail;
	}
}

/// Hash deliberadamente constante para producir un positivo Bloom falso reproducible.
struct ConstantHash {
	[[nodiscard]] constexpr eng::u32 operator()(eng::u32) const noexcept { return 0u; }
};

/// Hecho «disco d está en poste p» para Hanoi con tres discos.
constexpr u16 disk_fact(u16 disk, u16 pole) noexcept {
	return static_cast<u16>(disk * 3u + pole);
}

/// Genera las 18 acciones legales de movimiento para las tres piezas y tres postes.
constexpr eng::util::Array<Ai::Action, 18> make_hanoi_actions() {
	eng::util::Array<Ai::Action, 18> actions {};
	usize count = 0u;
	for (u16 disk = 0u; disk < 3u; ++disk) {
		for (u16 from = 0u; from < 3u; ++from) {
			for (u16 to = 0u; to < 3u; ++to) {
				if (from == to) continue;
				Ai::Action action {};
				action.cost = 1u;
				action.pre_true.facts.set(disk_fact(disk, from));
				for (u16 smaller = 0u; smaller < disk; ++smaller) {
					action.pre_false.facts.set(disk_fact(smaller, from));
					action.pre_false.facts.set(disk_fact(smaller, to));
				}
				action.eff_del.facts.set(disk_fact(disk, from));
				action.eff_add.facts.set(disk_fact(disk, to));
				actions[count++] = action;
			}
		}
	}
	return actions;
}

inline constexpr auto kActions = make_hanoi_actions();

/// Ejecuta el mismo Hanoi con la tabla exacta, comparando un Planner normal y Bloom.
void test_goap_equivalence_and_lookup_counts() {
	const Ai::State start = Ai::state(disk_fact(0u, 0u), disk_fact(1u, 0u), disk_fact(2u, 0u));
	Ai::Goal goal {};
	goal.want_true.facts.set(disk_fact(0u, 2u));
	goal.want_true.facts.set(disk_fact(1u, 2u));
	goal.want_true.facts.set(disk_fact(2u, 2u));

	DefaultPlanner exact {};
	SmallBloomPlanner filtered {};
	LargerBloomPlanner larger_filter {};
	u16 exact_plan[16] {};
	u16 bloom_plan[16] {};
	u16 larger_plan[16] {};
	const usize exact_length = exact.plan(start, goal, kActions.span(), {exact_plan, 16u});
	const usize bloom_length = filtered.plan(start, goal, kActions.span(), {bloom_plan, 16u});
	const usize larger_length = larger_filter.plan(start, goal, kActions.span(), {larger_plan, 16u});

	check(exact.found() && filtered.found(), "ambos planners encuentran una solución");
	check(exact_length == 7u && bloom_length == exact_length, "el filtro conserva el plan óptimo de Hanoi");
	check(exact.plan_cost() == 7u && filtered.plan_cost() == exact.plan_cost(),
	      "coste idéntico al planner exacto");
	check(exact.expansions() == filtered.expansions(), "misma secuencia de expansiones");
	check(larger_filter.found() && larger_length == exact_length &&
	      larger_filter.plan_cost() == exact.plan_cost() &&
	      larger_filter.expansions() == exact.expansions(), "filtro mayor conserva resultado y expansiones");
	bool same_plan = exact_length == bloom_length;
	for (usize i = 0u; i < exact_length && i < bloom_length; ++i) {
		same_plan = same_plan && exact_plan[i] == bloom_plan[i];
	}
	check(same_plan, "secuencia de acciones idéntica");
	bool same_larger_plan = exact_length == larger_length;
	for (usize i = 0u; i < exact_length && i < larger_length; ++i) {
		same_larger_plan = same_larger_plan && exact_plan[i] == larger_plan[i];
	}
	check(same_larger_plan, "también coincide la secuencia con el filtro mayor");

	Ai::State final_state = start;
	for (usize i = 0u; i < bloom_length; ++i) {
		const Ai::Action& action = kActions[bloom_plan[i]];
		check(eng::ai::applicable(final_state, action), "cada acción del plan Bloom es aplicable");
		eng::ai::apply(final_state, action);
	}
	check(eng::ai::satisfies(final_state, goal), "el plan Bloom termina en el objetivo");

	const eng::util::BloomFilterStats stats = filtered.bloom_stats();
	check(stats.queries > 0u, "el filtro registra candidatos generados");
	check(stats.negatives > 0u, "negativas omiten consultas al HashMap exacto");
	check(stats.possibles > 0u, "positivos probables consultan el HashMap exacto");
	check(stats.false_positives > 0u, "falsos positivos del Bloom pequeño se verifican exactamente");
	check(stats.queries == stats.negatives + stats.possibles,
	      "cada consulta se cuenta como negativa o posible");
	check(stats.false_positives <= stats.possibles,
	      "cada falso positivo pertenece a las posibles coincidencias");
	const eng::util::BloomFilterStats larger_stats = larger_filter.bloom_stats();
	check(larger_stats.false_positives == 0u && larger_stats.negatives > stats.negatives,
	      "filtro mayor reduce falsos positivos y omite más búsquedas en este escenario");
	#if !defined(ENG_AMIGA)
	std::printf("  GOAP small: candidatos=%u exactas=%u evitadas=%u falsos_positivos=%u bytes=%u\n",
		    static_cast<unsigned>(stats.queries), static_cast<unsigned>(stats.possibles),
			    static_cast<unsigned>(stats.negatives), static_cast<unsigned>(stats.false_positives),
			    static_cast<unsigned>(sizeof(eng::util::BloomFilter<Ai::State::Key, 32u>)));
	std::printf("  GOAP large: candidatos=%u exactas=%u evitadas=%u falsos_positivos=%u bytes=%u\n",
		    static_cast<unsigned>(larger_stats.queries), static_cast<unsigned>(larger_stats.possibles),
			    static_cast<unsigned>(larger_stats.negatives),
			    static_cast<unsigned>(larger_stats.false_positives),
			    static_cast<unsigned>(sizeof(eng::util::BloomFilter<Ai::State::Key, 256u>)));
	#endif

	// El caso por defecto no activa consultas ni añade bits al Planner.
	DefaultPlanner default_planner {};
	check(default_planner.bloom_stats().queries == 0u, "Bloom permanece apagado por defecto");
#if !defined(ENG_AMIGA)
	std::printf("  sizeof Planner: default=%u bloom256=%u bloom1024=%u\n",
		    static_cast<unsigned>(sizeof(DefaultPlanner)),
		    static_cast<unsigned>(sizeof(SmallBloomPlanner)),
		    static_cast<unsigned>(sizeof(LargerBloomPlanner)));
#endif
}

void test_bloom_no_false_negatives_and_forced_positive() {
	eng::util::BloomFilter<eng::u32, 256u> filter {};
	for (eng::u32 key = 0u; key < 96u; ++key) filter.insert(key);
	bool no_false_negatives = true;
	for (eng::u32 key = 0u; key < 96u; ++key) no_false_negatives = no_false_negatives && filter.may_contain(key);
	check(no_false_negatives, "ninguna clave insertada se informa ausente");

	eng::util::BloomFilter<eng::u32, 32u, 4u, ConstantHash> collision_filter {};
	eng::util::HashMap<eng::u32, eng::u8, 2u> exact {};
	collision_filter.insert(7u);
	(void)exact.insert(7u, 1u);
	const bool maybe_present = collision_filter.may_contain(99u);
	const bool exact_present = exact.contains(99u);
	if (maybe_present && !exact_present) collision_filter.note_false_positive();
	check(maybe_present && !exact_present, "hash constante produce positivo falso controlado");
	check(collision_filter.stats().false_positives == 1u,
	      "el consumidor puede contabilizar el positivo falso tras verificar exactamente");
	check(eng::util::BloomFilter<eng::u32, 256u>::storage_bytes == 32u,
	      "el tamaño del filtro queda fijado y visible en compilación");
}

} // namespace

int main() {
	#if !defined(ENG_AMIGA)
	std::printf("== HOST-415 GOAP Bloom ==\n");
	#endif
	test_goap_equivalence_and_lookup_counts();
	test_bloom_no_false_negatives_and_forced_positive();
	if (g_fail != 0u) {
	#if !defined(ENG_AMIGA)
		std::printf("FALLOS: %u\n", g_fail);
	#endif
		return 1;
	}
	#if !defined(ENG_AMIGA)
	std::printf("OK: Bloom sin falsos negativos; GOAP conserva resultado exacto.\n");
	#endif
	return 0;
}
