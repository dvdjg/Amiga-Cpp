// ============================================================================
// Test HOST-416: reparto de canales de sprite por ventanas de reprogramación.
// ============================================================================
//
// Valida en host el backbone puro del modelo híbrido
// (`eng/graphics/sprite_channel_window.hpp` + el overload con ledger de
// `eng/graphics/sprite_allocator.hpp`): reservar canales para un FONDO por sprites
// en un intervalo y repartir OBJETOS solo en los canales libres de ese intervalo,
// recuperando los 8 canales por encima y por debajo. Cada canal se reprograma de
// forma independiente, así que dos ventanas pueden solaparse en vertical si usan
// canales distintos.
//
// Comprobaciones:
//   1) Ledger: occupy/free, solape, corridas y máscara por línea.
//   2) plan_sprite_windows: ventanas válidas y errores (rango, canales, canal ocupado);
//      dos ventanas solapadas en vertical con canales DISJUNTOS son válidas.
//   3) Híbrido: ventana Risky Woods [60,100) usa 0..5; objetos del intervalo usan 6..7
//      (el 3.º degrada a BOB); fuera del intervalo se recuperan los 8 canales.
//   4) Attached y tiras respetan el ledger.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/416_sprite_channel_window   (solo este)
//   bash tools/run-host-tests.sh                                    (todos)

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/sprite_allocator.hpp>
#include <eng/graphics/sprite_channel_window.hpp>

namespace {

using eng::graphics::SpriteAllocator;
using eng::graphics::SpriteBackdropTechnique;
using eng::graphics::SpriteChannelLedger;
using eng::graphics::SpriteChannelWindow;
using eng::graphics::SpriteChannelWindowError;
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

SpriteChannelWindow make_window(eng::u16 top, eng::u16 bottom, eng::u8 first, eng::u8 count,
		     SpriteBackdropTechnique tech = SpriteBackdropTechnique::RiskyWoods,
		     bool attach = false) {
	SpriteChannelWindow w {};
	w.top = top;
	w.bottom = bottom;
	w.technique = tech;
	w.channel_first = first;
	w.channel_count = count;
	w.attach = attach;
	return w;
}

void test_ledger_basics() {
	std::printf("SpriteChannelLedger: occupy/free/solape/corrida/mascara\n");

	SpriteChannelLedger l {};
	l.reset();
	CHECK(l.free(0u, 10u, 20u));
	CHECK(l.occupy(0u, 60u, 100u));
	CHECK(l.free(0u, 10u, 20u));   // por debajo de la reserva
	CHECK(l.free(0u, 100u, 120u)); // justo despues (bottom exclusivo)
	CHECK(!l.free(0u, 99u, 101u)); // solapa
	CHECK(!l.free(0u, 70u, 80u));  // dentro
	CHECK(l.interval_count(0u) == 1u);

	// Corrida de 3 canales.
	CHECK(l.occupy_run(2u, 3u, 60u, 100u));
	CHECK(!l.free(2u, 70u, 80u) && !l.free(3u, 70u, 80u) && !l.free(4u, 70u, 80u));
	CHECK(l.free(5u, 70u, 80u));
	CHECK(l.free_run(3u, 70u, 80u) == 5u);
	CHECK(l.free_run(4u, 70u, 80u) == 0xffu); // solo quedan 5,6,7 en ese intervalo

	// Máscara por línea: en 70, ocupados 0 y 2..4 -> libres 1,5,6,7.
	CHECK(l.free_mask(70u) == 0xe2u);
	CHECK(l.free_mask(10u) == 0xffu);
}

void test_plan_windows_ok() {
	std::printf("plan_sprite_windows: ventanas validas (hibrido)\n");

	SpriteChannelWindow windows[3] {
		make_window(0u, 60u, 0u, 8u, SpriteBackdropTechnique::Layer),
		make_window(60u, 100u, 0u, 6u, SpriteBackdropTechnique::RiskyWoods),
		make_window(160u, 200u, 0u, 8u, SpriteBackdropTechnique::FreeForm),
	};
	SpriteChannelLedger l {};
	l.reset();
	const auto r = eng::graphics::plan_sprite_windows({windows, 3u}, l);
	CHECK(r.has_value());
	CHECK(r.value() == 3u);
	// En el intervalo del fondo a 6 canales, libres 6 y 7.
	CHECK(l.free_mask(70u) == 0xc0u);
	// En el hueco entre ventanas, los 8 canales.
	CHECK(l.free_mask(120u) == 0xffu);
	// La ventana Free Form usa los 8: ninguno libre.
	CHECK(l.free_mask(170u) == 0x00u);
}

void test_plan_windows_errors() {
	std::printf("plan_sprite_windows: errores de rango/canales/ocupado\n");

	{
		SpriteChannelLedger l {};
		l.reset();
		SpriteChannelWindow w[1] { make_window(60u, 60u, 0u, 4u) }; // rango vacio
		const auto r = eng::graphics::plan_sprite_windows({w, 1u}, l);
		CHECK(!r.has_value() && r.error() == SpriteChannelWindowError::BadRange);
	}
	{
		SpriteChannelLedger l {};
		l.reset();
		SpriteChannelWindow w[1] { make_window(60u, 100u, 6u, 4u) }; // 6..9 se sale de 0..7
		const auto r = eng::graphics::plan_sprite_windows({w, 1u}, l);
		CHECK(!r.has_value() && r.error() == SpriteChannelWindowError::BadChannels);
	}
	{
		SpriteChannelLedger l {};
		l.reset();
		// Una reserva previa (p. ej. de otro sistema) ocupa 0..1 en [40,55).
		CHECK(l.occupy_run(0u, 2u, 40u, 55u));
		SpriteChannelWindow w[1] { make_window(40u, 55u, 0u, 2u) };
		const auto r = eng::graphics::plan_sprite_windows({w, 1u}, l);
		CHECK(!r.has_value() && r.error() == SpriteChannelWindowError::ChannelBusy);
	}
	{
		SpriteChannelLedger l {};
		l.reset();
		// Dos ventanas que comparten canal en líneas solapadas: conflicto por canal.
		SpriteChannelWindow w[2] {
			make_window(60u, 100u, 0u, 4u),
			make_window(90u, 120u, 2u, 4u), // 2..3 ya ocupados por la primera
		};
		const auto r = eng::graphics::plan_sprite_windows({w, 2u}, l);
		CHECK(!r.has_value() && r.error() == SpriteChannelWindowError::ChannelBusy);
	}
	{
		SpriteChannelLedger l {};
		l.reset();
		// Attached sobre un canal impar: no forma pares completos.
		SpriteChannelWindow w[1] { make_window(60u, 100u, 1u, 2u, SpriteBackdropTechnique::RiskyWoods, true) };
		const auto r = eng::graphics::plan_sprite_windows({w, 1u}, l);
		CHECK(!r.has_value() && r.error() == SpriteChannelWindowError::BadChannels);
	}
}

void test_overlapping_windows_disjoint_channels() {
	std::printf("plan_sprite_windows: solape vertical con canales disjuntos es valido\n");

	// Cada canal se reprograma de forma independiente: dos ventanas pueden solapar en
	// vertical si usan canales distintos. 0..3 y 4..7 solapan en [90,100).
	SpriteChannelWindow w[2] {
		make_window(60u, 100u, 0u, 4u),
		make_window(90u, 120u, 4u, 4u),
	};
	SpriteChannelLedger l {};
	l.reset();
	const auto r = eng::graphics::plan_sprite_windows({w, 2u}, l);
	CHECK(r.has_value());
	CHECK(r.value() == 2u);
	// En [90,100) los dos fondos juntos ocupan los 8 canales.
	CHECK(l.free_mask(95u) == 0x00u);
	// En [100,120) solo la segunda ventana (4..7): libres 0..3.
	CHECK(l.free_mask(110u) == 0x0fu);
}

void test_hybrid_objects_around_backdrop() {
	std::printf("Hibrido: fondo Risky Woods [60,100) deja 2 canales a los objetos\n");

	SpriteChannelWindow window[1] { make_window(60u, 100u, 0u, 6u) };
	SpriteChannelLedger l {};
	l.reset();
	CHECK(eng::graphics::plan_sprite_windows({window, 1u}, l).has_value());

	// Intents ordenados por top: 2 arriba, 3 dentro del fondo, 2 abajo.
	SpriteIntent intents[7] {
		make_intent(10u, 40u),   // A -> canal 0
		make_intent(20u, 40u),   // B -> canal 1
		make_intent(70u, 90u),   // C -> canal 6 (0..5 son del fondo)
		make_intent(70u, 90u),   // D -> canal 7
		make_intent(70u, 90u),   // E -> BOB (no queda canal en el intervalo)
		make_intent(120u, 140u), // F -> canal 0 (reutilizado)
		make_intent(130u, 140u), // G -> canal 1
	};
	SpriteSlot slots[7] {};

	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 7u}, slots, l);
	CHECK(in_hw == 6u);

	CHECK(!slots[0].as_bob && slots[0].channel == 0u);
	CHECK(!slots[1].as_bob && slots[1].channel == 1u);
	CHECK(!slots[2].as_bob && slots[2].channel == 6u);
	CHECK(!slots[3].as_bob && slots[3].channel == 7u);
	CHECK(slots[4].as_bob);                    // el 3.º del intervalo degrada a BOB
	CHECK(!slots[5].as_bob && slots[5].channel == 0u); // recupera el 0 por debajo
	CHECK(!slots[6].as_bob && slots[6].channel == 1u);
}

void test_hybrid_attached_and_strip() {
	std::printf("Hibrido: par attached y tira respetan el ledger\n");

	// Fondo que ocupa 0..3 en [60,100): deja 4..7 para objetos.
	SpriteChannelWindow window[1] { make_window(60u, 100u, 0u, 4u) };
	SpriteChannelLedger l {};
	l.reset();
	CHECK(eng::graphics::plan_sprite_windows({window, 1u}, l).has_value());

	// El asignador se llama UNA vez por frame con TODOS los objetos: la ocupación de
	// objetos vive en la pasada (busy_until), no en el ledger (que es solo de fondos).
	// Por eso par y tira van en la misma lista, ordenada por top.
	SpriteIntent intents[5] {};
	intents[0] = make_intent(70u, 90u);            // par attached, lider (par)
	intents[1] = make_intent(70u, 90u);            // par attached, impar
	intents[1].attach = true;
	for (int i = 2; i < 5; ++i) {                  // tira de 3 en el mismo intervalo
		intents[i] = make_intent(70u, 90u);
		intents[i].strip_id = 1u;
		intents[i].strip_index = static_cast<eng::u8>(i - 2);
		intents[i].strip_span = 3u;
	}
	SpriteSlot slots[5] {};
	const eng::u8 hw = SpriteAllocator{}.assign({intents, 5u}, slots, l);
	// Par a 4/5 (primer par libre); la tira no encuentra corrida de 3 -> entera a BOB.
	CHECK(hw == 2u);
	CHECK(!slots[0].as_bob && slots[0].channel == 4u);
	CHECK(!slots[1].as_bob && slots[1].channel == 5u);
	CHECK(slots[2].as_bob && slots[3].as_bob && slots[4].as_bob);
}

void test_empty_ledger_matches_classic() {
	std::printf("Hibrido: ledger vacio = reparto clasico de 8 canales\n");

	// Sin ledger, 9 solapados -> 8 en hardware + 1 BOB (comportamiento historico).
	SpriteIntent intents[9];
	for (int i = 0; i < 9; ++i) intents[i] = make_intent(50u, 80u);
	SpriteSlot slots[9] {};
	const eng::u8 in_hw = SpriteAllocator{}.assign({intents, 9u}, slots);
	CHECK(in_hw == 8u);
	CHECK(slots[8].as_bob);

	// Con un ledger vacio explicito, identico.
	SpriteChannelLedger empty {};
	empty.reset();
	SpriteSlot slots2[9] {};
	const eng::u8 in_hw2 = SpriteAllocator{}.assign({intents, 9u}, slots2, empty);
	CHECK(in_hw2 == 8u);
	CHECK(slots2[8].as_bob);
}

} // namespace

int main() {
	std::printf("Test HOST-416 sprite_channel_window (ventanas de reprogramacion)\n");
	std::printf("================================================================\n");

	test_ledger_basics();
	test_plan_windows_ok();
	test_plan_windows_errors();
	test_overlapping_windows_disjoint_channels();
	test_hybrid_objects_around_backdrop();
	test_hybrid_attached_and_strip();
	test_empty_ledger_matches_classic();

	if (g_failures == 0) {
		std::printf("OK: reparto de sprites por ventanas de reprogramacion validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
