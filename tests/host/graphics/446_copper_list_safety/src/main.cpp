// ============================================================================
// Test HOST-446: seguridad de copperlist (slots tipados + validadores + presupuesto).
// ============================================================================
//
// Cubre la capa de compilación/setup que impide "instrucciones no válidas" y listas
// descuadradas (ver docs/engine/architecture/COHERENCIA_DE_SETUP.md):
//
//   1) Slots tipados: `move_at` devuelve `MoveSlot`, `wait_*` devuelve `WaitSlot`, y los
//      parches (`patch_data`/`move_reg`/`wait` libres y de `Scheduler`) escriben la palabra
//      correcta. Mezclar tipos no compila (negativo estático por diseño del tipo).
//   2) `eng::setup::check_copper_list`: acepta una lista real del `Scheduler` y rechaza
//      sin END, MOVEs a registros no escribibles (solo lectura/reservado/otro chipset),
//      instrucciones tras el END, terminador mal formado y WAITs que retroceden.
//   3) `eng::setup::check_copper_column`: valida el paso de un relleno por columnas
//      (blitter strided / copper-chunky) contra las instrucciones emitidas.
//   4) Presupuesto en compilación (`StaticCopperList` de `compile_intents`): `static_assert`
//      sobre líneas visibles por encima del presupuesto y MOVEs de la línea más cargada.
//   5) `copper_register_access`/`copper_register_writable`: mapa por chipset (OCS/ECS/AGA).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/446_copper_list_safety

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/copper/static_plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/memory/arena.hpp>
#include <eng/setup/coherence.hpp>

namespace {

using eng::u16;
using eng::copper::ListBuilder;
using eng::copper::MoveSlot;
using eng::copper::Register;
using eng::copper::RegisterAccess;
using eng::copper::WaitSlot;
using eng::hw::Chipset;

int g_failures = 0;

#define CHECK(cond, ...)                                                  \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

void check_result(const eng::setup::CheckResult& r, eng::u8 code, const char* what) {
	if (r.code != code) {
		std::printf("  [FAIL] %s: code=%u esperado=%u (%s)\n", what, r.code, code, r.message);
		++g_failures;
	}
}

eng::u16 g_words[256] {};
eng::u16 g_palette[8] {}; ///< fuente de paleta del caso positivo (valores irrelevantes)

// ---------------------------------------------------------------------------------------
// 1) Slots tipados y helpers de dominio
// ---------------------------------------------------------------------------------------
void test_typed_slots() {
	// Helpers: offsets derivados, sin numeros magicos en el llamador.
	CHECK(eng::copper::color_register(1u).offset == 0x182u);
	CHECK(eng::copper::sprite_pos_register(3u).offset == 0x158u);
	CHECK(eng::copper::sprite_ctl_register(7u).offset == 0x17au);
	CHECK(eng::copper::sprite_pointer_high_register(2u).offset == 0x128u);
	CHECK(eng::copper::sprite_pointer_low_register(2u).offset == 0x12au);
	CHECK(eng::copper::bitplane_pointer_high_register(5u).offset == 0x0f4u);
	CHECK(eng::copper::bitplane_pointer_low_register(5u).offset == 0x0f6u);

	for (u16& w : g_words) {
		w = 0u;
	}
	eng::MemoryBlock block {g_words, sizeof(g_words), eng::MemoryKind::Chip};
	ListBuilder b {block};
	const MoveSlot color = b.move_at(eng::copper::color_register(5u), 0x0123u);
	CHECK(color.valid(), "move_at devuelve un slot valido");
	CHECK(g_words[color.opcode_word()] == 0x18au, "opcode = COLOR05");
	CHECK(g_words[color.data_word()] == 0x0123u, "dato inicial");

	// Parche libre (bloques activo/inactivo) y `MoveSlot::data_word`.
	const eng::Span<u16> span {g_words, sizeof(g_words) / 2u};
	eng::copper::patch_data(span, color, 0x0abcu);
	eng::copper::patch_move_reg(span, color, eng::copper::color_register(7u));
	CHECK(g_words[color.data_word()] == 0x0abcu, "patch_data escribe el DATO");
	CHECK(g_words[color.opcode_word()] == 0x18eu,
	      "patch_move_reg escribe el destino");

	// `move32` (par de MOVEs) y su parche: PTH/PTL van en instrucciones consecutivas.
	const MoveSlot pair = b.move32(Register::COP2LCH, eng::Address<eng::MemoryKind::Chip> {0x00012345u});
	CHECK(g_words[pair.opcode_word()] == static_cast<u16>(Register::COP2LCL), "move32 low primero");
	CHECK(g_words[pair.next_instruction().opcode_word()] == static_cast<u16>(Register::COP2LCH),
	      "move32 high despues");
	eng::copper::patch_move32(span, pair, 0x00abcdefu);
	CHECK(g_words[pair.data_word()] == 0xcdefu &&
	      g_words[pair.next_instruction().data_word()] == 0x00abu,
	      "patch_move32 reparte low/high");

	// WAIT tipado: el parche de línea solo acepta `WaitSlot` (un indice de MOVE no compila).
	const WaitSlot wait = b.wait_line(0x40u);
	CHECK(g_words[wait.opcode_word()] == 0x4001u && g_words[wait.mask_word()] == 0xff00u,
	      "wait_line codificado");
	eng::copper::patch_wait(span, wait, 0x50u);
	CHECK(g_words[wait.opcode_word()] == 0x5001u, "patch_wait reescribe la linea");

	// `patch_wait` con vpos > 255 no puede resolver el overflow: el builder queda no valido.
	ListBuilder bad {block};
	bad.wait_line(0x10u);
	const WaitSlot w2 = bad.wait_line(0x20u);
	bad.patch_wait(w2, 300u);
	CHECK(!bad.ok(), "patch_wait con vpos > 255 marca el builder no valido");
}

// ---------------------------------------------------------------------------------------
// 2) check_copper_list: caso positivo (lista real) y negativos
// ---------------------------------------------------------------------------------------
void test_check_list() {
	// Lista real emitida por el Scheduler (display minimo + paleta + WAIT + END).
	for (u16& w : g_words) {
		w = 0u;
	}
	eng::MemoryBlock block {g_words, sizeof(g_words), eng::MemoryKind::Chip};
	eng::copper::Scheduler sched {block};
	sched.move(Register::DMACON, static_cast<u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						      eng::copper::DmaCopper | eng::copper::DmaBitplane));
	sched.move(Register::BPLCON0, 0x4200u);
	sched.emit_palette(eng::PaletteWords {g_palette, 8u}, 0u, 8u); // 8 colores (valores irrelevantes aqui)
	sched.move(Register::COLOR00, 0x0000u);
	sched.wait_line(0xf8u);
	sched.move(Register::COLOR00, 0x0000u);
	sched.end();
	const u16 words = sched.words_used();
	CHECK(sched.ok(), "scheduler ok");
	CHECK(eng::setup::check_copper_list(sched).ok(), "overload de emisor (Scheduler)");
	const auto ok_list = eng::setup::check_copper_list(sched.data(), words, Chipset::OCS);
	if (!ok_list.ok()) {
		std::printf("  [FAIL] lista valida rechazada: code=%u word=%u (%s)\n", ok_list.code,
			    ok_list.index, ok_list.message);
		++g_failures;
	}
	CHECK(eng::setup::check_copper_list(nullptr, 0u, Chipset::OCS).code == 1u,
	      "lista nula -> sin terminador");

	// Sin END: un MOVE y nada mas.
	{
		const u16 w[2] = {static_cast<u16>(Register::COLOR00), 0x0000u};
		check_result(eng::setup::check_copper_list(w, 2u, Chipset::OCS), 1u, "sin END");
	}
	// Instrucciones tras el END.
	{
		const u16 w[6] = {0xffffu, 0xfffeu, static_cast<u16>(Register::COLOR00), 0x0001u,
				  0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 6u, Chipset::OCS), 4u, "tras END");
	}
	// Terminador mal formado (espera infinita).
	{
		const u16 w[2] = {0xffffu, 0xffffu};
		check_result(eng::setup::check_copper_list(w, 2u, Chipset::OCS), 5u, "END mal formado");
	}
	// MOVE a registro de solo lectura (DMACONR $002) y a hueco (UHRES $076).
	{
		const u16 w[4] = {0x0002u, 0x0100u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 4u, Chipset::OCS), 2u, "registro solo lectura");
	}
	{
		const u16 w[4] = {0x0076u, 0x0100u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 4u, Chipset::OCS), 2u, "hueco del mapa");
	}
	// Registro ECS en OCS se rechaza; en ECS se acepta.
	{
		const u16 w[4] = {static_cast<u16>(Register::BPLCON3), 0x0001u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 4u, Chipset::OCS), 2u, "BPLCON3 en OCS");
		CHECK(eng::setup::check_copper_list(w, 4u, Chipset::ECS).ok(), "BPLCON3 en ECS");
	}
	// Registro AGA en OCS/ECS se rechaza; en AGA se acepta.
	{
		const u16 w[4] = {static_cast<u16>(Register::BPLCON4), 0x0011u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 4u, Chipset::OCS), 2u, "BPLCON4 en OCS");
		check_result(eng::setup::check_copper_list(w, 4u, Chipset::ECS), 2u, "BPLCON4 en ECS");
		CHECK(eng::setup::check_copper_list(w, 4u, Chipset::AGA).ok(), "BPLCON4 en AGA");
	}
	// MOVE con bits no canonicos (la lista real solo escribe A9-A1).
	{
		const u16 w[4] = {static_cast<u16>(0x8000u | 0x0180u), 0x0001u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 4u, Chipset::OCS), 6u, "bits altos");
	}
	// Numero impar de palabras (instruccion partida).
	{
		const u16 w[3] = {static_cast<u16>(Register::COLOR00), 0x0000u, 0xffffu};
		check_result(eng::setup::check_copper_list(w, 3u, Chipset::OCS), 7u, "cuenta impar");
	}
	// WAIT que retrocede: 0x50 y luego 0x20.
	{
		const u16 w[8] = {0x5001u, 0xff00u, static_cast<u16>(Register::COLOR00), 0x0000u,
				  0x2001u, 0xff00u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 8u, Chipset::OCS), 3u, "WAIT retrocede");
	}
	// El par de overflow PAL reinicia la comparacion (linea baja tras 0xffdf).
	{
		const u16 w[8] = {0xffdfu, 0xfffeu, 0x2001u, 0xff00u,
				  static_cast<u16>(Register::COLOR00), 0x0000u, 0xffffu, 0xfffeu};
		CHECK(eng::setup::check_copper_list(w, 8u, Chipset::OCS).ok(),
		      "par de overflow reinicia el orden");
	}
	// Lista con **reinicio de tramo** (COPJMP2, patron copper-chunky/plasma): el recorrido
	// monotono de WAITs no aplica (la lista se re-ejecuta desde un label), pero estructura y
	// registros se siguen validando.
	{
		const u16 w[10] = {0x5001u, 0xff00u,                              // WAIT 0x50
				   static_cast<u16>(Register::COLOR00), 0x0001u,
				   0x2001u, 0xff00u,                              // WAIT 0x20 (retrocede)
				   static_cast<u16>(Register::COPJMP2), 0x0000u,  // reinicio de tramo
				   0xffffu, 0xfffeu};
		CHECK(eng::setup::check_copper_list(w, 10u, Chipset::OCS).ok(),
		      "COPJMP2 exime del orden monotono de WAITs");
	}
	{
		const u16 w[6] = {0x0002u, 0x0100u, // DMACONR: solo lectura
				  static_cast<u16>(Register::COPJMP2), 0x0000u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 6u, Chipset::OCS), 2u,
			     "COPJMP2 no exime de registros no escribibles");
	}
	// Geometria de fetch (DDF): pareja alineada valida; desalineada -> 8; invertida -> 9.
	{
		const u16 w[8] = {static_cast<u16>(Register::DDFSTRT), 0x0038u,
				  static_cast<u16>(Register::DDFSTOP), 0x00d0u,
				  static_cast<u16>(Register::COLOR00), 0x0000u, 0xffffu, 0xfffeu};
		CHECK(eng::setup::check_copper_list(w, 8u, Chipset::OCS).ok(),
		      "pareja DDF 0x38/0xd0 valida");
	}
	// Dos tramos (mode-switch): cada DDFSTOP se empareja con su DDFSTRT, sin comparar
	// entre zonas (el DDFSTOP de la 2.ª zona es menor que el de la 1.ª, y es valido).
	{
		const u16 w[12] = {static_cast<u16>(Register::DDFSTRT), 0x0038u,
				   static_cast<u16>(Register::DDFSTOP), 0x00d0u,
				   static_cast<u16>(Register::DDFSTRT), 0x0040u,
				   static_cast<u16>(Register::DDFSTOP), 0x00c8u,
				   static_cast<u16>(Register::COLOR00), 0x0000u, 0xffffu, 0xfffeu};
		CHECK(eng::setup::check_copper_list(w, 12u, Chipset::OCS).ok(),
		      "parejas DDF por tramos");
	}
	{
		const u16 w[6] = {static_cast<u16>(Register::DDFSTRT), 0x003au, // no multiplo de 4
				  static_cast<u16>(Register::DDFSTOP), 0x00d0u, 0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 6u, Chipset::OCS), 8u,
			     "DDFSTRT desalineado");
	}
	{
		const u16 w[6] = {static_cast<u16>(Register::DDFSTRT), 0x0038u,
				  static_cast<u16>(Register::DDFSTOP), 0x00d2u, // no multiplo de 4
				  0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 6u, Chipset::OCS), 8u,
			     "DDFSTOP desalineado");
	}
	{
		const u16 w[6] = {static_cast<u16>(Register::DDFSTRT), 0x0040u,
				  static_cast<u16>(Register::DDFSTOP), 0x0030u, // < DDFSTRT
				  0xffffu, 0xfffeu};
		check_result(eng::setup::check_copper_list(w, 6u, Chipset::OCS), 9u,
			     "ventana DDF invertida");
	}
	// `DDFSTOP == DDFSTRT` es la ventana de 1 palabra que describe el propio AHRM
	// (`DDFSTRT = DDFSTOP - 8*(palabras-1)`): se admite.
	{
		const u16 w[6] = {static_cast<u16>(Register::DDFSTRT), 0x0038u,
				  static_cast<u16>(Register::DDFSTOP), 0x0038u, 0xffffu, 0xfffeu};
		CHECK(eng::setup::check_copper_list(w, 6u, Chipset::OCS).ok(),
		      "DDF de 1 palabra (stop == strt) admitido");
	}
}

// ---------------------------------------------------------------------------------------
// 3) check_copper_column: relleno con paso validado contra las instrucciones
// ---------------------------------------------------------------------------------------
void test_check_column() {
	// Lista estilo copper-chunky: WAIT + 4 MOVEs COLOR00 consecutivos (datos con paso 2).
	for (u16& w : g_words) {
		w = 0u;
	}
	eng::MemoryBlock block {g_words, sizeof(g_words), eng::MemoryKind::Chip};
	ListBuilder b {block};
	b.wait_line(0x2cu);
	const MoveSlot first = b.move_at(Register::COLOR00, 0x1111u);
	(void)b.move_at(Register::COLOR00, 0x2222u);
	(void)b.move_at(Register::COLOR00, 0x3333u);
	(void)b.move_at(Register::COLOR00, 0x4444u);
	b.end();
	const u16 words = b.words_used();

	const eng::copper::ColumnPatch ok_patch = eng::copper::column_patch(first, 4u);
	CHECK(eng::setup::check_copper_column(b.data(), words, ok_patch).ok(),
	      "columna stride 2 valida");
	CHECK(ok_patch.data_word(3u) == first.data_word() + 6u, "data_word(r) con stride 2");

	// Stride equivocado (4): la tercera fila cae en el terminador de la lista.
	const eng::copper::ColumnPatch bad_stride {first, 4u, 4u};
	check_result(eng::setup::check_copper_column(b.data(), words, bad_stride), 3u,
		     "stride 4 cae en el terminador");
	// Forma no representable (paso impar).
	const eng::copper::ColumnPatch odd_stride {first, 3u, 2u};
	check_result(eng::setup::check_copper_column(b.data(), words, odd_stride), 1u,
		     "stride impar");
	// Destino que no es dato de MOVE: primer slot desplazado a una palabra de OPCODE.
	// `first` con index 3 apunta al dato del 1.er MOVE; su `data_word` (4) cae en el
	// opcode del 2.º MOVE, y la comprobacion mira la palabra anterior (3 = dato, 0x1111).
	{
		const eng::copper::ColumnPatch shifted {MoveSlot {3u}, 2u, 1u};
		// El valor previo es el dato 0x1111 (impar): no es un opcode de MOVE.
		check_result(eng::setup::check_copper_column(b.data(), words, shifted), 3u,
			     "destino no es dato de MOVE");
	}
}

// ---------------------------------------------------------------------------------------
// 4) Presupuesto en compilacion (compile_intents + static_assert)
// ---------------------------------------------------------------------------------------
using eng::graphics::CopperIntent;
using eng::graphics::CopperIntentKind;

constexpr eng::u16 kPal1[1] = {0x00fu};
constexpr eng::u16 kPal2[2] = {0x0f0u, 0xf00u};

/// Escena ligera: 3 bandas de 1 color en lineas distintas -> 1 MOVE por linea, sin spill.
constexpr CopperIntent kLight[] = {
	{CopperIntentKind::PaletteLine, 0x2cu, 0x2cu, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x30u, 0x30u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x34u, 0x34u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
};
constexpr auto kLightList = eng::copper::compile_intents<128u>(kLight, {40u, 4u, 0x2cu});
static_assert(kLightList.ok, "la escena ligera compila");
static_assert(kLightList.visible_over_budget_lines == 0u, "sin spill visible");
static_assert(kLightList.heaviest_line_moves == 1u, "una linea = 1 MOVE");
static_assert(kLightList.words[0] == eng::copper::wait_word(0x2c), "primer WAIT de la lista");

/// Escena "malvada": 40 bandas de 1 color EN LA MISMA linea visible -> la Timeline la
/// rechazaria (presupuesto HBlank 20): el `static_assert` lo congela en compilacion.
constexpr CopperIntent kHeavy[] = {
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
	{CopperIntentKind::PaletteLine, 0x40u, 0x40u, 0u, eng::PaletteWords {kPal1, 1u}, 0u, 1u, 0, {}, 0u, nullptr, nullptr},
};
constexpr auto kHeavyList = eng::copper::compile_intents<256u>(kHeavy, {40u, 4u, 0x2cu});
static_assert(kHeavyList.ok, "la escena malvada compila igualmente");
static_assert(kHeavyList.visible_over_budget_lines == 1u,
	      "el presupuesto de la Timeline la habria rechazado");
static_assert(kHeavyList.heaviest_line_moves == 40u, "40 MOVEs en la linea 0x40");

void test_static_budget() {
	CHECK(kLightList.words[0] == eng::copper::wait_word(0x2c), "WAIT de la primera banda");
	const auto stat = eng::setup::check_copper_list(kLightList.words.data(), kLightList.word_count,
							Chipset::OCS);
	if (!stat.ok()) {
		std::printf("  [FAIL] lista compile_intents rechazada: code=%u word=%u (%s)\n",
			    stat.code, stat.index, stat.message);
		++g_failures;
	}
	// Ejercicio runtime de los static_assert (si compila, se cumplen).
	CHECK(kLightList.visible_over_budget_lines == 0u && kHeavyList.visible_over_budget_lines == 1u,
	      "presupuesto en compilacion");
}

// ---------------------------------------------------------------------------------------
// 5) Mapa de registros por chipset y reporte publicable
// ---------------------------------------------------------------------------------------
void test_register_map_and_report() {
	CHECK(eng::copper::copper_register_access(0x180u) == RegisterAccess::Writable, "COLOR00");
	CHECK(eng::copper::copper_register_access(0x180u + 62u) == RegisterAccess::Writable, "COLOR31");
	CHECK(eng::copper::copper_register_access(0x1c0u) == RegisterAccess::EcsOnly, "HTOTAL ECS");
	CHECK(eng::copper::copper_register_access(0x10cu) == RegisterAccess::AgaOnly, "BPLCON4 AGA");
	CHECK(eng::copper::copper_register_access(0x002u) == RegisterAccess::ReadOnly, "DMACONR");
	CHECK(eng::copper::copper_register_access(0x076u) == RegisterAccess::Reserved, "hueco");
	CHECK(eng::copper::copper_register_writable(0x058u, Chipset::OCS), "BLTSIZE (CDANG del takeover)");
	CHECK(!eng::copper::copper_register_writable(0x1c0u, Chipset::OCS), "HTOTAL no en OCS");
	CHECK(eng::copper::copper_register_writable(0x1c0u, Chipset::AGA), "HTOTAL en AGA");

	eng::copper::ScheduleReport r {};
	r.ok = true;
	CHECK(r.publishable(), "report ok publicable");
	r.has_visible_timeline_spill = true;
	CHECK(!r.publishable(), "spill visible no publicable");
	r.has_visible_timeline_spill = false;
	r.unhandled_intents = 1u;
	CHECK(!r.publishable(), "intents sin manejar no publicable");
	CHECK(!eng::copper::strict_mode || r.publishable() == false,
	      "strict_mode es constexpr (opt-in por build)");
}

/// `ListBuilder` con una reserva menor que un par (1 palabra) o vacía: rechaza el MOVE y deja
/// los canarios intactos (el par WAIT/MOVE necesita 2 palabras; capacidad 0 envolvía `cap-2`).
void test_list_builder_capacity() {
	alignas(4) eng::u16 buf[4] {0xAAAu, 0x1234u, 0xBBBu, 0xCCCu};
	const eng::Block<eng::CopperTag> one_word {
		eng::Bytes<eng::CopperTag> {reinterpret_cast<eng::u8*>(&buf[1]), 2u},
		eng::MemoryKind::Chip};
	ListBuilder b {one_word};
	b.move(Register::COLOR00, 0x0fffu);
	CHECK(!b.ok(), "ListBuilder con 1 palabra rechaza el MOVE");
	CHECK(buf[0] == 0xAAAu && buf[1] == 0x1234u && buf[2] == 0xBBBu && buf[3] == 0xCCCu,
	      "canarios intactos con capacidad 1");

	const eng::Block<eng::CopperTag> empty {eng::Bytes<eng::CopperTag> {}, eng::MemoryKind::Chip};
	ListBuilder b2 {empty};
	b2.move(Register::COLOR00, 0x0fffu);
	CHECK(!b2.ok(), "ListBuilder vacío no escribe");
}

} // namespace

int main() {
	test_typed_slots();
	test_check_list();
	test_check_column();
	test_static_budget();
	test_register_map_and_report();
	test_list_builder_capacity();
	if (g_failures != 0) {
		std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
		return 1;
	}
	std::printf("OK: seguridad de copperlist (slots tipados, validadores, columnas, presupuesto).\n");
	return 0;
}
