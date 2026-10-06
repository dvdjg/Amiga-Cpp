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
//   4) Tiras horizontales (corrida contigua o BOB entera).
//   5) Pares attached, cadenas verticales, canal preferido, prioridad de asignación
//      (`assign_rank`) y grupos con trayectoria (corrida para el bounding box o BOB entero).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/003_sprite_allocator   (solo este)
//   bash tools/run-host-tests.sh                                    (todos)

#include <cstdio>

#include <eng/core/types/types.hpp>
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 6}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 9}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 6}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 2}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 3}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 3}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 9}, slots);
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

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 2}, slots);
	CHECK(in_hw == 0u);
	CHECK(slots[0].as_bob && slots[1].as_bob);
}

void test_attached_pair() {
	std::printf("SpriteAllocator: par attached en canal par\n");

	// Par attached (15 colores): el par (even) + el impar (`attach = true`).
	SpriteIntent intents[2] {};
	intents[0] = make_intent(40, 80);
	intents[1] = make_intent(40, 80);
	intents[1].attach = true;
	SpriteSlot slots[2] {};
	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 2}, slots);
	CHECK(in_hw == 2u);
	CHECK(!slots[0].as_bob && !slots[1].as_bob);
	CHECK(slots[0].channel % 2u == 0u);
	CHECK(slots[1].channel == static_cast<eng::u8>(slots[0].channel + 1u));

	// Un suelto ocupa el canal 0; el par attached debe ir al 2/3 (canal par).
	SpriteIntent intents2[3] {};
	intents2[0] = make_intent(0, 200);
	intents2[1] = make_intent(0, 200);
	intents2[2] = make_intent(0, 200);
	intents2[2].attach = true;
	SpriteSlot slots2[3] {};
	const eng::u8 hw2 = SpriteAllocator{}.assign({intents2, 3}, slots2);
	CHECK(hw2 == 3u);
	CHECK(slots2[0].channel == 0u);
	CHECK(slots2[1].channel == 2u && slots2[2].channel == 3u);

	// Sin hueco par libre (0..7 ocupados por solapados), el par no cabe.
	SpriteIntent intents3[10] {};
	for (int i = 0; i < 8; ++i) intents3[i] = make_intent(0, 200);
	intents3[8] = make_intent(0, 200);
	intents3[9] = make_intent(0, 200);
	intents3[9].attach = true;
	SpriteSlot slots3[10] {};
	const eng::u8 hw3 = SpriteAllocator{}.assign({intents3, 10}, slots3);
	CHECK(hw3 == 8u);
	CHECK(slots3[8].as_bob && slots3[9].as_bob);
}

void test_vertical_chain() {
	std::printf("SpriteAllocator: cadena vertical en un solo canal\n");

	// Dos franjas del MISMO objeto (cadena) + un suelto intercalado: el suelto no puede
	// robar el canal que la cadena reserva para todo su rango.
	SpriteIntent intents[4] {};
	intents[0] = make_intent(40, 60);
	intents[0].chain_id = 5u;
	intents[0].chain_index = 0u;
	intents[0].chain_span = 3u;
	intents[1] = make_intent(50, 70); // suelto intercalado
	intents[2] = make_intent(80, 100);
	intents[2].chain_id = 5u;
	intents[2].chain_index = 1u;
	intents[2].chain_span = 3u;
	intents[3] = make_intent(120, 140);
	intents[3].chain_id = 5u;
	intents[3].chain_index = 2u;
	intents[3].chain_span = 3u;

	SpriteSlot slots[4] {};
	const eng::u8 hw = SpriteAllocator{}.assign({intents, 4}, slots);
	CHECK(hw == 4u);
	CHECK(!slots[0].as_bob && slots[0].channel == 0u);
	CHECK(!slots[1].as_bob && slots[1].channel == 1u); // suelto en otro canal
	CHECK(!slots[2].as_bob && slots[2].channel == 0u); // franja 2: canal de la cadena
	CHECK(!slots[3].as_bob && slots[3].channel == 0u); // franja 3: canal de la cadena

	// El hueco ENTRE franjas tampoco se cede: la reserva cubre el rango completo.
	SpriteIntent hole[3] {};
	hole[0] = make_intent(40, 60);
	hole[0].chain_id = 7u;
	hole[0].chain_index = 0u;
	hole[0].chain_span = 2u;
	hole[1] = make_intent(62, 78); // sonda en el hueco
	hole[2] = make_intent(80, 100);
	hole[2].chain_id = 7u;
	hole[2].chain_index = 1u;
	hole[2].chain_span = 2u;
	SpriteSlot hole_slots[3] {};
	const eng::u8 hw2 = SpriteAllocator{}.assign({hole, 3}, hole_slots);
	CHECK(hw2 == 3u);
	CHECK(hole_slots[0].channel == 0u);
	CHECK(hole_slots[1].channel != 0u);
	CHECK(hole_slots[2].channel == 0u);

	// Dos cadenas intercaladas conviven en canales distintos.
	SpriteIntent two[4] {};
	two[0] = make_intent(10, 30);
	two[0].chain_id = 1u;
	two[0].chain_span = 2u;
	two[1] = make_intent(20, 40);
	two[1].chain_id = 2u;
	two[1].chain_span = 2u;
	two[2] = make_intent(50, 70);
	two[2].chain_id = 1u;
	two[2].chain_index = 1u;
	two[2].chain_span = 2u;
	two[3] = make_intent(60, 80);
	two[3].chain_id = 2u;
	two[3].chain_index = 1u;
	two[3].chain_span = 2u;
	SpriteSlot two_slots[4] {};
	const eng::u8 hw3 = SpriteAllocator{}.assign({two, 4}, two_slots);
	CHECK(hw3 == 4u);
	CHECK(two_slots[0].channel == 0u && two_slots[1].channel == 1u);
	CHECK(two_slots[2].channel == 0u && two_slots[3].channel == 1u);

	// Ocho canales ocupados: la cadena no cabe y degrada ENTERA (nadie queda a medias).
	SpriteIntent full[11] {};
	for (int i = 0; i < 8; ++i) full[i] = make_intent(0, 200);
	for (int i = 8; i < 11; ++i) {
		full[i] = make_intent(0, 200);
		full[i].chain_id = 9u;
		full[i].chain_index = static_cast<eng::u8>(i - 8);
		full[i].chain_span = 3u;
	}
	SpriteSlot full_slots[11] {};
	const eng::u8 hw4 = SpriteAllocator{}.assign({full, 11}, full_slots);
	CHECK(hw4 == 8u);
	CHECK(full_slots[8].as_bob && full_slots[9].as_bob && full_slots[10].as_bob);
}

void test_preferred_channel() {
	std::printf("SpriteAllocator: canal preferido\n");

	SpriteIntent intents[2] {};
	intents[0] = make_intent(10, 20);
	intents[0].channel = 3u;
	intents[1] = make_intent(10, 20); // solapa con el primero: el 3 está ocupado
	intents[1].channel = 3u;
	SpriteSlot slots[2] {};
	const eng::u8 hw = SpriteAllocator{}.assign({intents, 2}, slots);
	CHECK(hw == 2u);
	CHECK(slots[0].channel == 3u);
	CHECK(slots[1].channel != 3u);
}

void test_assign_rank() {
	std::printf("SpriteAllocator: prioridad de asignacion (fijos primero)\n");

	// El fijo (rank 1) tiene top MAYOR que el libre y solapan: sin prioridad, el libre
	// se llevaría el canal 0 por orden de Y.
	SpriteIntent intents[2] {};
	intents[0] = make_intent(40, 60); // libre (rank 0), prefiere 0
	intents[0].channel = 0u;
	intents[1] = make_intent(50, 70); // fijo (rank 1), prefiere 0
	intents[1].channel = 0u;
	intents[1].assign_rank = 1u;
	SpriteSlot slots[2] {};
	const eng::u8 hw = SpriteAllocator{}.assign({intents, 2}, slots);
	CHECK(hw == 2u);
	CHECK(slots[1].channel == 0u);
	CHECK(slots[0].channel == 1u);

	// Con ocupación EXACTA, un libre sin solape SÍ reutiliza el canal del fijo aunque se
	// asigne después (un `lastY[8]` conservador lo habría degradado).
	SpriteIntent apart[2] {};
	apart[0] = make_intent(10, 30);
	apart[0].channel = 0u;
	apart[1] = make_intent(50, 70);
	apart[1].channel = 0u;
	apart[1].assign_rank = 1u;
	SpriteSlot apart_slots[2] {};
	const eng::u8 hw2 = SpriteAllocator{}.assign({apart, 2}, apart_slots);
	CHECK(hw2 == 2u);
	CHECK(apart_slots[1].channel == 0u && apart_slots[0].channel == 0u);
}

void test_trajectory_group() {
	std::printf("SpriteAllocator: grupo con trayectoria (corrida contigua)\n");

	// Formación de 3 naves con tops 40/44/48 y alto 12; base preferida 2. Un libre que
	// empieza dentro del bounding box NO puede robar la corrida.
	SpriteIntent intents[4] {};
	intents[0] = make_intent(40, 52);
	intents[0].group_id = 4u;
	intents[0].group_index = 0u;
	intents[0].group_span = 3u;
	intents[0].channel = 2u;
	intents[0].assign_rank = 1u;
	intents[1] = make_intent(42, 54); // libre
	intents[2] = make_intent(44, 56);
	intents[2].group_id = 4u;
	intents[2].group_index = 1u;
	intents[2].group_span = 3u;
	intents[2].assign_rank = 1u;
	intents[3] = make_intent(48, 60);
	intents[3].group_id = 4u;
	intents[3].group_index = 2u;
	intents[3].group_span = 3u;
	intents[3].assign_rank = 1u;
	SpriteSlot slots[4] {};
	const eng::u8 hw = SpriteAllocator{}.assign({intents, 4}, slots);
	CHECK(hw == 4u);
	CHECK(slots[0].channel == 2u && slots[2].channel == 3u && slots[3].channel == 4u);
	CHECK(slots[1].channel == 0u); // fuera de la corrida reservada

	// Grupo que no cabe (solo quedan 2 canales para una corrida de 3): TODO a BOB.
	SpriteIntent full[9] {};
	for (int i = 0; i < 6; ++i) {
		full[i] = make_intent(0, 200);
		full[i].assign_rank = 2u; // ocupan 0..5 antes que el grupo
	}
	for (int i = 6; i < 9; ++i) {
		full[i] = make_intent(0, 200);
		full[i].group_id = 9u;
		full[i].group_index = static_cast<eng::u8>(i - 6);
		full[i].group_span = 3u;
		full[i].channel = 6u;
		full[i].assign_rank = 1u;
	}
	SpriteSlot full_slots[9] {};
	const eng::u8 hw2 = SpriteAllocator{}.assign({full, 9}, full_slots);
	CHECK(hw2 == 6u);
	CHECK(full_slots[6].as_bob && full_slots[7].as_bob && full_slots[8].as_bob);
}

void test_aga_wide_sprite() {
	std::printf("SpriteAllocator: width_words=2 (AGA 32 px) usa 1 canal\n");

	// El ancho de 32 px no cambia el nº de canales ni la asignación: sigue siendo 1 por
	// sprite, y el multiplexado vertical (bottom <= top reusa canal) no se altera.
	SpriteIntent intents[2] {};
	intents[0] = make_intent(10, 20);
	intents[0].width_words = 2u; // AGA: 32 px
	intents[1] = make_intent(30, 40);
	intents[1].width_words = 1u;
	SpriteSlot slots[2] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 2}, slots);
	CHECK(in_hw == 2u);
	CHECK(!slots[0].as_bob && slots[0].channel == 0u);
	CHECK(!slots[1].as_bob && slots[1].channel == 0u); // reusa el canal (no solapan)
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
	test_attached_pair();
	test_vertical_chain();
	test_preferred_channel();
	test_assign_rank();
	test_trajectory_group();
	test_aga_wide_sprite();

	if (g_failures == 0) {
		std::printf("OK: asignador de sprites validado (multiplexado/overflow/reuso).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
