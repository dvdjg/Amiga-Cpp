// ============================================================================
// Test HOST-317: bucle de planificacion *anytime* por frames (`eng::sim`)
// ============================================================================
//
// Consumidor real del presupuesto: una criatura encadena acciones (0 -> 1 -> ... -> 5) y
// **no** puede gastar toda la busqueda en un frame. En cada frame pide un plan con
// presupuesto (`apply_budget`); si sale **parcial** lo ejecuta un paso y sigue. Se valida
// que:
//
//   1) alcanza el objetivo repartiendo el trabajo en varios frames;
//   2) ejecuta exactamente los pasos del plan;
//   3) **ninguna** busqueda supera `PlanParams::budget` (lo que acota el coste por frame).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/sim/317_sim_anytime_loop

#include <cstdio>

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

void test_anytime_loop() {
	using Ai = SimGoap; // 32 hechos, sin variables
	constexpr eng::util::Array<Ai::Action, 5> acts {{
	    Ai::Builder {}.require(0u).produce(1u).build(),
	    Ai::Builder {}.require(1u).produce(2u).build(),
	    Ai::Builder {}.require(2u).produce(3u).build(),
	    Ai::Builder {}.require(3u).produce(4u).build(),
	    Ai::Builder {}.require(4u).produce(5u).build(),
	}};
	Ai::Goal goal {};
	goal.want_true.facts.set(static_cast<eng::ai::Fact>(5u));
	Ai::State state {};
	state.facts.set(static_cast<eng::ai::Fact>(0u));

	PlanParams params {};
	params.budget = 2u; // dos expansiones por busqueda
	PlannerDriver<64, 8> driver;
	apply_budget(driver, params);

	eng::usize steps = 0u;
	eng::usize frames = 0u;
	eng::usize max_exp = 0u;
	while (!eng::ai::satisfies(state, goal) && frames < 20u) {
		++frames;
		if (!driver.has_plan() && !driver.replan(state, goal, acts.span())) {
			break;
		}
		if (driver.expansions() > max_exp) {
			max_exp = driver.expansions();
		}
		const eng::u16 a = driver.current();
		if (a == 0xffffu) {
			break;
		}
		eng::ai::apply(state, acts[a]);
		driver.advance();
		++steps;
	}

	check(eng::ai::satisfies(state, goal), "anytime: alcanza el objetivo por frames");
	check(steps == 5u, "anytime: ejecuta los 5 pasos del plan");
	check(frames > 1u, "anytime: reparte el trabajo en mas de un frame");
	check(max_exp <= params.budget, "anytime: ninguna busqueda supera el presupuesto");
}

} // namespace

int main() {
	std::printf("Sim anytime loop:\n");
	test_anytime_loop();

	if (g_fail == 0u) {
		std::printf("OK: Sim bucle anytime (parciales por frame, pasos y presupuesto)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
