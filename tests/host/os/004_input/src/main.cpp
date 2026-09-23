// ============================================================================
// Test HOST-004: entrada unificada (eng::input::InputAggregator).
// ============================================================================
//
// Valida en host la abstracción de entrada portable del engine (paso 6 de
// ENGINE_DESIGN.md §5): `PadState`, `MouseState`, `KeyState` e `InputAggregator`.
// Son datos puros (sin hardware), por eso se prueban con g++.
//
// Comprobaciones:
//   1) Los tipos son POD (trivialmente copiables, portables).
//   2) El estado por defecto es "sin entrada".
//   3) Los helpers (any_direction/any_button/any) reflejan el estado.

#include <cstdio>
#include <type_traits>

#include <eng/core/types.hpp>
#include <eng/input/input.hpp>

namespace {

using eng::input::InputAggregator;
using eng::input::KeyState;
using eng::input::MouseState;
using eng::input::PadState;

static_assert(std::is_trivially_copyable_v<PadState>);
static_assert(std::is_trivially_copyable_v<MouseState>);
static_assert(std::is_trivially_copyable_v<KeyState>);
static_assert(std::is_trivially_copyable_v<InputAggregator>);

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

void test_default_empty() {
	std::printf("input: estado por defecto sin entrada\n");

	InputAggregator a {};
	CHECK(a.any() == false);
	CHECK(a.pad0.any() == false);
	CHECK(a.pad0.any_direction() == false);
	CHECK(a.pad0.any_button() == false);
}

void test_pad_state() {
	std::printf("input: PadState direcciones y botones CD32\n");

	PadState p {};
	p.right = true;
	p.fire = true;
	CHECK(p.any_direction() == true);
	CHECK(p.any_button() == true);
	CHECK(p.any() == true);

	// fire2/play/yellow/green son botones de color independientes.
	PadState cd32 {};
	cd32.fire2 = true;
	cd32.play = true;
	cd32.yellow = true;
	cd32.green = true;
	CHECK(cd32.any_button() == true);
	CHECK(cd32.fire == false);  // el rojo (fire) no está pulsado
}

void test_aggregator_any() {
	std::printf("input: InputAggregator agrega pads/ratón/teclado\n");

	InputAggregator a {};
	CHECK(a.any() == false);

	a.pad1.up = true;
	CHECK(a.any() == true);

	a = {};
	a.mouse.left_button = true;
	CHECK(a.any() == true);

	a = {};
	a.keys.pending = 0x02u;  // scancode '1'
	CHECK(a.any() == true);
}

} // namespace

int main() {
	std::printf("Test HOST-004 input\n");
	std::printf("===================\n");

	test_default_empty();
	test_pad_state();
	test_aggregator_any();

	if (g_failures == 0) {
		std::printf("OK: entrada unificada validada (InputAggregator/PadState/MouseState/KeyState).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
