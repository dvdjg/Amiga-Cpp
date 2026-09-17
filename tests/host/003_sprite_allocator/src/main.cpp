// ============================================================================
// Test HOST-003: asignador de canales de sprite (SpriteAllocator).
// ============================================================================
//
// Valida en host el asignador de canales de sprite del engine
// (`eng/graphics/sprite_allocator.hpp`, paso 4 de ENGINE_DESIGN.md §5): reparte
// `SpriteIntent` entre los 8 canales hardware con multiplexado vertical y decide
// el overflow → BOB. Es lógica pura (sin hardware), por eso se prueba con g++.
//
// Comprobaciones:
//   1) Multiplexado vertical: sprites sin solape vertical comparten canal.
//   2) Overflow horizontal: más de 8 sprites solapados desbordan a `as_bob`.
//   3) Mixto: sprites que solapan consumen canales distintos; los que no
//      solapan reutilizan.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/003_sprite_allocator   (solo este)
//   bash tools/run-host-tests.sh                                    (todos)

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/sprite_allocator.hpp>

namespace {

using eng::graphics::SpriteAllocator;
using eng::graphics::SpriteIntent;
using eng::graphics::SpriteSlot;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

SpriteIntent make_intent(eng::u16 top, eng::u16 bottom) {
	SpriteIntent it {};
	it.top = top;
	it.bottom = bottom;
	return it;
}

void test_vertical_multiplexing() {
	std::printf("SpriteAllocator: multiplexado vertical (sin solape -> mismo canal)\n");

	// 6 sprites apilados con 1 línea de separación, sin solape vertical.
	SpriteIntent intents[6] {
		make_intent(40, 63),  make_intent(65, 88),  make_intent(90, 113),
		make_intent(115, 138), make_intent(140, 163), make_intent(165, 188),
	};
	SpriteSlot slots[6] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 6, slots);
	CHECK(in_hw == 6u);
	// Sin solape vertical, el first-fit los apila todos en el canal 0.
	for (int i = 0; i < 6; ++i) {
		CHECK(slots[i].as_bob == false);
		CHECK(slots[i].channel == 0u);
	}
}

void test_horizontal_overflow() {
	std::printf("SpriteAllocator: overflow horizontal (>8 solapados -> BOB)\n");

	// 9 sprites TODOS solapados verticalmente (misma franja).
	SpriteIntent intents[9];
	for (int i = 0; i < 9; ++i) intents[i] = make_intent(50, 80);
	SpriteSlot slots[9] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 9, slots);
	CHECK(in_hw == 8u);
	// Los 8 primeros ocupan canales distintos; el noveno desborda a BOB.
	for (int i = 0; i < 8; ++i) {
		CHECK(slots[i].as_bob == false);
		CHECK(slots[i].channel == static_cast<eng::u8>(i));
	}
	CHECK(slots[8].as_bob == true);
}

void test_mixed_reuse() {
	std::printf("SpriteAllocator: reutiliza canales libres entre franjas\n");

	// 3 sprites solapados arriba + 3 solapados abajo (no solapan entre grupos).
	SpriteIntent intents[6] {
		make_intent(10, 30), make_intent(10, 30), make_intent(10, 30),
		make_intent(120, 140), make_intent(120, 140), make_intent(120, 140),
	};
	SpriteSlot slots[6] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 6, slots);
	CHECK(in_hw == 6u);
	// Primer grupo ocupa canales 0,1,2; el segundo los reutiliza (no solapa).
	for (int i = 0; i < 3; ++i) CHECK(slots[i].channel == static_cast<eng::u8>(i));
	for (int i = 3; i < 6; ++i) CHECK(slots[i].channel == static_cast<eng::u8>(i - 3));
	CHECK(slots[5].as_bob == false);
}

void test_no_overflow_boundary() {
	std::printf("SpriteAllocator: el canal queda libre justo en el límite\n");

	// Un sprite termina en 63 y el siguiente empieza en 64: no solapan
	// (busy_until 63 < top 64), así que comparten canal.
	SpriteIntent intents[2] { make_intent(40, 63), make_intent(64, 87) };
	SpriteSlot slots[2] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 2, slots);
	CHECK(in_hw == 2u);
	CHECK(slots[0].channel == slots[1].channel);
}

void test_horizontal_strip() {
	std::printf("SpriteAllocator: tira horizontal de canales contiguos\n");

	// Tira de 3 tramos contiguos (fondo ancho tipo Risky Woods) en la misma franja.
	SpriteIntent intents[3] {};
	for (int i = 0; i < 3; ++i) {
		intents[i] = make_intent(40, 80);
		intents[i].strip_id = 1u;
		intents[i].strip_index = static_cast<eng::u8>(i);
		intents[i].strip_span = 3u;
	}
	SpriteSlot slots[3] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 3, slots);
	CHECK(in_hw == 3u);
	CHECK(!slots[0].as_bob && !slots[1].as_bob && !slots[2].as_bob);
	CHECK(slots[0].channel == 0u && slots[1].channel == 1u && slots[2].channel == 2u);
}

void test_strip_starts_after_busy_channel() {
	std::printf("SpriteAllocator: la tira busca la primera corrida libre\n");

	// Un sprite suelto ocupa el canal 0; la tira de 2 debe empezar en el 1.
	SpriteIntent intents[3] {};
	intents[0] = make_intent(0, 200);
	intents[1] = make_intent(0, 200);
	intents[1].strip_id = 1u;
	intents[1].strip_index = 0u;
	intents[1].strip_span = 2u;
	intents[2] = make_intent(0, 200);
	intents[2].strip_id = 1u;
	intents[2].strip_index = 1u;
	intents[2].strip_span = 2u;
	SpriteSlot slots[3] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 3, slots);
	CHECK(in_hw == 3u);
	CHECK(slots[0].channel == 0u);
	CHECK(slots[1].channel == 1u && slots[2].channel == 2u);
}

void test_strip_overflow_to_bob() {
	std::printf("SpriteAllocator: tira que no cabe entera -> BOB\n");

	// 6 sueltos solapados ocupan los canales 0..5; una tira de 3 no tiene corrida.
	SpriteIntent intents[9] {};
	for (int i = 0; i < 6; ++i) intents[i] = make_intent(0, 200);
	for (int i = 6; i < 9; ++i) {
		intents[i] = make_intent(0, 200);
		intents[i].strip_id = 2u;
		intents[i].strip_index = static_cast<eng::u8>(i - 6);
		intents[i].strip_span = 3u;
	}
	SpriteSlot slots[9] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 9, slots);
	CHECK(in_hw == 6u);
	CHECK(slots[6].as_bob && slots[7].as_bob && slots[8].as_bob);
}

void test_strip_bad_order_rejected() {
	std::printf("SpriteAllocator: tira sin líder (indice 0) -> BOB\n");

	// El primer miembro de una tira debe ser el índice 0; si no, se rechaza entera.
	SpriteIntent intents[2] {};
	intents[0] = make_intent(0, 200);
	intents[0].strip_id = 1u;
	intents[0].strip_index = 1u;
	intents[0].strip_span = 2u;
	intents[1] = make_intent(0, 200);
	intents[1].strip_id = 1u;
	intents[1].strip_index = 0u;
	intents[1].strip_span = 2u;
	SpriteSlot slots[2] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign(intents, 2, slots);
	CHECK(in_hw == 0u);
	CHECK(slots[0].as_bob && slots[1].as_bob);
}

} // namespace

int main() {
	std::printf("Test HOST-003 sprite_allocator\n");
	std::printf("================================\n");

	test_vertical_multiplexing();
	test_horizontal_overflow();
	test_mixed_reuse();
	test_no_overflow_boundary();
	test_horizontal_strip();
	test_strip_starts_after_busy_channel();
	test_strip_overflow_to_bob();
	test_strip_bad_order_rejected();

	if (g_failures == 0) {
		std::printf("OK: asignador de sprites validado (multiplexado/overflow/reuso).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
