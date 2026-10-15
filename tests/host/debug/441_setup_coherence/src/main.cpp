// ============================================================================
// Test HOST-441: validadores de coherencia de setup (`eng/setup/coherence.hpp`).
// ============================================================================
//
// Respalda el sistema de coherencia (`docs/engine/architecture/COHERENCIA_DE_SETUP.md`):
// cada validador puro se ejercita con **los fallos históricos reales** como casos:
//
//   - BPLCON0 con BPU=5 y 4 planos emitidos (el «plano fantasma» de la demo 066);
//   - BPLCON0 sin bit COLOR (0x4000, divergencia silenciosa en RGB);
//   - WAIT del Copper que retrocede (las bandas de la 065 que «no se veían»);
//   - hoja de BOB que rebasa su bloque (offset mal calculado → invade la región siguiente);
//   - región fuera del banco de memoria.
//
//   CXX=<g++ del entorno> bash tools/run-host-tests.sh tests/host/debug/441_setup_coherence

#include <cstdio>

#include <eng/setup/coherence.hpp>

namespace {

int g_fail = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_fail;                                                      \
		}                                                                 \
	} while (0)

using eng::setup::CheckResult;
using eng::setup::RegionTable;

using eng::graphics::composition::SceneMode;
using eng::graphics::composition::bplcon0_for;

void test_regions() {
	std::printf("-- regiones: solape parcial, contencion, banco, desbordamiento\n");

	// Caso sano (el mapa de la 066): bloques adyacentes + hoja contenida en su bloque.
	RegionTable<8> ok;
	CHECK(ok.add("bitplanes", 0x15060u, 0xa000u)); // +0xA000 = 0x1F060
	CHECK(ok.add("copper", 0x1f060u, 0x1000u));
	CHECK(ok.add("bob-block", 0x20060u, 12288u));
	CHECK(ok.add("sheet-orb", 0x20060u + 0u, 60u)); // dentro del bloque: contencion
	CHECK(ok.validate().ok());
	CHECK(ok.validate_within(0x10000u, 0x20000u).ok());

	// Solape parcial: hoja que empieza dentro del bloque y lo rebasa (invade lo siguiente).
	RegionTable<4> partial;
	CHECK(partial.add("bob-block", 0x20060u, 12288u));
	CHECK(partial.add("sheet-fugada", 0x20060u + 12280u, 40u));
	const CheckResult p = partial.validate();
	CHECK(!p.ok() && p.code == 1u);
	CHECK(p.index == 1u); // la segunda region es la culpable

	// Fuera del banco: el copper se sale del Chip RAM configurado.
	RegionTable<2> out;
	CHECK(out.add("copper", 0x7f000u, 0x2000u)); // termina en 0x81000 > 0x80000
	const CheckResult o = out.validate_within(0x00000u, 0x80000u);
	CHECK(!o.ok() && o.code == 2u);

	// Desbordamiento de direccion (base + bytes da la vuelta a 32 bits).
	RegionTable<2> wrap;
	CHECK(wrap.add("imposible", 0xfffff000u, 0x2000u));
	const CheckResult w = wrap.validate();
	CHECK(!w.ok() && w.code == 3u);

	// Tabla llena: add devuelve false y no registra.
	RegionTable<1> full;
	CHECK(full.add("a", 0x1000u, 16u));
	CHECK(!full.add("b", 0x2000u, 16u));
	CHECK(full.count() == 1u);
}

void test_bplcon0() {
	std::printf("-- BPLCON0 contra planos: el bug del plano fantasma\n");

	// Historico 066: BPU=5 (0x5200) con 4 planos emitidos.
	const CheckResult ghost = eng::setup::check_bplcon0(0x5200u, 4u);
	CHECK(!ghost.ok() && ghost.code == 1u);
	CHECK(ghost.expected == 4u && ghost.actual == 5u);

	// Historico 066 (2): 0x4000 sin bit COLOR (RGB no lo nota; el canon si).
	const CheckResult nocolor = eng::setup::check_bplcon0(0x4000u, 4u);
	CHECK(!nocolor.ok() && nocolor.code == 2u);

	// El valor canonico de 4 planos pasa.
	CHECK(eng::setup::check_bplcon0(bplcon0_for(SceneMode::Standard, 4u), 4u).ok());
	CHECK(bplcon0_for(SceneMode::Standard, 4u) == 0x4200u);

	// HAM exige 6 planos; DBLPF exige par; EHB = 6 planos con COLOR.
	CHECK(eng::setup::check_bplcon0(bplcon0_for(SceneMode::Ham, 4u), 4u).code == 3u);
	CHECK(eng::setup::check_bplcon0(bplcon0_for(SceneMode::DualPlayfield, 5u), 5u).code == 4u);
	CHECK(eng::setup::check_bplcon0(bplcon0_for(SceneMode::Ehb, 6u), 6u).ok());

	// Modo vs BPLCON0: EHB con 4 planos y HAM sin bit HAM.
	CHECK(eng::setup::check_bplcon0_mode(0x4200u, SceneMode::Ehb, 4u).code == 5u);
	CHECK(eng::setup::check_bplcon0_mode(0x6200u, SceneMode::Ham, 6u).code == 6u);
	CHECK(eng::setup::check_bplcon0_mode(bplcon0_for(SceneMode::Ham, 6u), SceneMode::Ham, 6u).ok());
}

void test_copper_wait_order() {
	std::printf("-- orden de WAITs del Copper\n");

	// Lista sana: WAIT 0x2c -> MOVE -> WAIT 0x64 -> MOVE -> END.
	const eng::u16 ok_list[] = {
		0x2c01u, 0xff00u, // WAIT linea 0x2c
		0x0180u, 0x0123u, // MOVE COLOR00
		0x6401u, 0xff00u, // WAIT linea 0x64
		0x0180u, 0x0456u, // MOVE COLOR00
		0xffffu, 0xffffu, // END
	};
	CHECK(eng::setup::check_copper_wait_order(ok_list, 10u).ok());

	// Historico 065: una banda esperaba a la 0x4c DESPUES de un WAIT en la 0x76.
	const eng::u16 bad_list[] = {
		0x7601u, 0xff00u, // WAIT 0x76
		0x0180u, 0x0111u, // MOVE
		0x4c01u, 0xff00u, // WAIT 0x4c (retrocede)
		0x0180u, 0x0222u, // MOVE
		0xffffu, 0xffffu,
	};
	const CheckResult bad = eng::setup::check_copper_wait_order(bad_list, 10u);
	CHECK(!bad.ok() && bad.code == 1u);
	CHECK(bad.index == 4u); // indice de la palabra del WAIT culpable

	// El par de overflow PAL (0xffdf/0xfffe) reinicia el orden: linea 0x20 tras 0xf8 es legal.
	const eng::u16 wrap_ok[] = {
		0xf801u, 0xff00u, // WAIT 0xf8
		0xffdfu, 0xfffeu, // marcador de wrap (CopWaitSafe)
		0x2001u, 0xfffeu, // WAIT 0x20 tras el wrap
		0xffffu, 0xffffu,
	};
	CHECK(eng::setup::check_copper_wait_order(wrap_ok, 8u).ok());

	// Sin el marcador, el mismo 0x20 tras 0xf8 retrocede.
	const eng::u16 wrap_bad[] = {
		0xf801u, 0xff00u,
		0x2001u, 0xfffeu,
		0xffffu, 0xffffu,
	};
	CHECK(eng::setup::check_copper_wait_order(wrap_bad, 6u).code == 1u);

	// Tras el END no se mira nada (palabras residuales del buffer no cuentan).
	const eng::u16 after_end[] = {
		0x2c01u, 0xff00u,
		0xffffu, 0xffffu,
		0x1001u, 0xff00u, // basura tras el END: ignorada
	};
	CHECK(eng::setup::check_copper_wait_order(after_end, 6u).ok());

	// WAIT con posicion horizontal (wait_position): mismo V y H creciente es legal.
	const eng::u16 hpos_list[] = {
		0x4c03u, 0xfffeu, // WAIT 0x4c, H=0x02
		0x4c0fu, 0xfffeu, // WAIT 0x4c, H=0x0e
		0xffffu, 0xffffu,
	};
	CHECK(eng::setup::check_copper_wait_order(hpos_list, 6u).ok());
}

void test_bob_plan() {
	std::printf("-- combinacion de BOB cubierta por el ejecutor\n");

	using eng::graphics::Bob;
	using eng::graphics::BobDraw;
	using eng::graphics::BobLayout;
	using eng::graphics::BobMaskPack;

	// Historico 066: cookie-cut + hoja declarada Interleaved + mascara en plano suelto.
	// `bob_draw` devolvia `false` en silencio: el caminante y el patrullero no se dibujaban.
	Bob bad {};
	bad.draw = BobDraw::CookieCut;
	bad.layout = BobLayout::Interleaved;
	bad.mask_pack = BobMaskPack::SeparatePlane;
	CHECK(eng::setup::check_bob_plan(bad).code == 1u);

	// Cookie-cut planar con mascara: el camino que SI cubre el ejecutor.
	Bob ok {};
	ok.draw = BobDraw::CookieCut;
	ok.layout = BobLayout::Planar;
	ok.mask = eng::ChipView<eng::BobTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(
			reinterpret_cast<const eng::u8*>(0x1000u)),
		64u};
	CHECK(eng::setup::check_bob_plan(ok).ok());

	// Cookie-cut sin mascara: rechazado.
	Bob nomask {};
	nomask.draw = BobDraw::CookieCut;
	nomask.layout = BobLayout::Planar;
	CHECK(eng::setup::check_bob_plan(nomask).code == 2u);

	// Or: el layout de la hoja no interviene (el dibujado usa el del target).
	Bob or_bob {};
	or_bob.draw = BobDraw::Or;
	or_bob.layout = BobLayout::Interleaved;
	CHECK(eng::setup::check_bob_plan(or_bob).ok());

	// Formato de la HOJA vs formato del TARGET: el ejecutor deriva la lectura de la hoja del
	// layout del destino; un formato distinto se lee mal en silencio (codigo 3).
	eng::graphics::BobTarget tgt {};
	tgt.layout = BobLayout::Planar;
	CHECK(eng::setup::check_bob_plan(or_bob, tgt).code == 3u);
	tgt.layout = BobLayout::Interleaved;
	CHECK(eng::setup::check_bob_plan(or_bob, tgt).ok());
}

void test_sprite_line_data() {
	std::printf("check_sprite_line_data: parcheo de DATA a media linea vs planos\n");

	// Historico Brian the Lion (sprite-tricks-games.md): 4/5 planos dejan hueco de DMA para
	// parchear SPRxDATA a media linea; con 6 no (custom.cpp:10111/10130-10131).
	CHECK(eng::setup::check_sprite_line_data(4u).ok());
	CHECK(eng::setup::check_sprite_line_data(5u).ok());
	CHECK(eng::setup::check_sprite_line_data(6u).code == 1u);
	CHECK(eng::setup::check_sprite_line_data(6u).expected ==
	      eng::graphics::kSpriteLineDataMaxBitplanes);
}

void test_sprite_line_hpos() {
	std::printf("check_sprite_line_hpos: rearmado por linea tras el fetch de sprites\n");

	// La X del primer tramo del rearmado debe caer tras el fetch (DDFSTRT/2 ~ 28 px de
	// pantalla; custom.cpp:10111). HSTART absoluto: minimo 0x81 + 28 = 157.
	CHECK(eng::setup::check_sprite_line_hpos(161u).ok()); // x de pantalla 32
	CHECK(eng::setup::check_sprite_line_hpos(157u).ok()); // justo en el limite
	CHECK(eng::setup::check_sprite_line_hpos(156u).code == 1u);
	CHECK(eng::setup::check_sprite_line_hpos(100u).code == 1u);
	CHECK(eng::setup::check_sprite_line_hpos(100u).expected == 157u);
}

} // namespace

int main() {
	std::printf("Test HOST-441 coherencia de setup (regiones, BPLCON0, orden de WAITs)\n");
	std::printf("========================================================================\n");

	test_regions();
	test_bplcon0();
	test_copper_wait_order();
	test_bob_plan();
	test_sprite_line_data();
	test_sprite_line_hpos();

	if (g_fail == 0) {
		std::printf("OK: validadores de coherencia verificados con los fallos historicos.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_fail);
	return 1;
}
