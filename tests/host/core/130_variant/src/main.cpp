// ============================================================================
// Test HOST-130: union etiquetada sin heap (Variant) usada como cola de comandos
// ============================================================================
//
// Valida `engine/include/eng/core/util/variant.hpp` con un consumidor tipico:
// comandos heterogeneos de juego (mover, atacar, esperar) que se despachan con `visit`.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/130_variant

#include <cstdio>

#include <eng/core/util/variant.hpp>

namespace {

using eng::s16;
using eng::u16;

unsigned g_fail = 0;
void check(bool ok, const char* what) {
	if (!ok) {
		std::printf("[FAIL] %s\n", what);
		++g_fail;
	}
}

struct Move {
	s16 dx;
	s16 dy;
};
struct Attack {
	s16 target;
};
struct Wait {
	u16 ticks;
};

using Command = eng::util::Variant<Move, Attack, Wait>;

struct State {
	s16 x = 0;
	s16 y = 0;
	s16 target = -1;
	u16 waited = 0;
};

void apply(State& st, const Command& cmd) {
	cmd.visit([&](const auto& c) {
		using T = eng::util::remove_cvref_t<decltype(c)>;
		if constexpr (eng::util::is_same_v<T, Move>) {
			st.x = static_cast<s16>(st.x + c.dx);
			st.y = static_cast<s16>(st.y + c.dy);
		} else if constexpr (eng::util::is_same_v<T, Attack>) {
			st.target = c.target;
		} else {
			st.waited = static_cast<u16>(st.waited + c.ticks);
		}
	});
}

void test_commands() {
	State st;
	const Command c1 {Move {2, 3}};
	check(c1.index() == 0u && c1.holds<Move>() && !c1.holds<Attack>(),
	      "variant: primer alternativo");
	check(c1.get<Move>().dx == 2 && c1.get<Move>().dy == 3, "variant: get directo");
	apply(st, c1);
	check(st.x == 2 && st.y == 3, "variant: el comando Move se aplica");

	const Command c2 {Attack {7}};
	check(c2.index() == 1u && c2.holds<Attack>() && !c2.holds<Move>(),
	      "variant: segundo alternativo");
	apply(st, c2);
	check(st.target == 7, "variant: Attack se aplica");

	const Command c3 {Wait {5}};
	check(c3.holds<Wait>(), "variant: tercer alternativo");
	apply(st, c3);
	check(st.waited == 5u, "variant: Wait se aplica");

	// `emplace` cambia el alternativo activo en el mismo objeto.
	Command c4 {Move {1, 1}};
	c4.emplace(Wait {9});
	check(c4.index() == 2u && c4.holds<Wait>() && c4.get<Wait>().ticks == 9u,
	      "variant: emplace reasigna");

	// El constructor por defecto deja la primera alternativa.
	const Command d;
	check(d.index() == 0u && d.holds<Move>(), "variant: default = primera");
}

} // namespace

int main() {
	std::printf("Variant:\n");
	test_commands();

	if (g_fail == 0u) {
		std::printf("OK: Variant (comandos heterogeneos: index/holds/get/visit/emplace)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
