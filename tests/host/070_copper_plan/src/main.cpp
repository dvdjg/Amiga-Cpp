// ============================================================================
// Test HOST-070: `copper::Plan` (orquestacion de copper a nivel de escena)
// ============================================================================
//
// Valida en host (sin Amiga) lo que el plan aporta sobre emitir copper a mano:
//
//   1) `begin`/`begin_frame` reservan el doble buffer y situan el emisor DETRAS.
//   2) `materialize` ORDENA las intenciones por scanline (el llamador puede
//      anadirlas en cualquier orden; el scheduler exige orden ascendente).
//   3) `end_frame` escribe SIEMPRE en el bloque trasero y voltea: la lista activa
//      del frame anterior no se toca (invariante anti-tearing).
//   4) `takeover`/`install` publican el bloque activo (swap de COP1LC).
//   5) El overflow de intenciones no publica una lista parcial (`end_frame` false).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/070_copper_plan

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u16;
using eng::u32;

struct MockBackend {
	const u16* taken = nullptr;
	const u16* installed = nullptr;
	unsigned installs = 0;
	void takeover_display(const u16* words) { taken = words; }
	void install_copper_list(const u16* words) {
		installed = words;
		++installs;
	}
};

alignas(16) eng::u8 g_chip[64 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

/// Numero de MOVEs a `reg` en la lista.
unsigned count_moves(const u16* words, u16 count, u16 reg) {
	unsigned n = 0;
	for (u16 i = 0; i + 1u < count; i += 2u) {
		const u16 w0 = words[i];
		if (w0 == 0xffffu) break;
		if ((w0 & 1u) != 0u) continue; // WAIT
		if (w0 == reg) ++n;
	}
	return n;
}

/// Lineas de los WAITs, en orden de aparicion.
unsigned wait_lines(const u16* words, u16 count, u16* out, unsigned max) {
	unsigned n = 0;
	for (u16 i = 0; i + 1u < count && n < max; i += 2u) {
		const u16 w0 = words[i];
		if (w0 == 0xffffu) break;
		if ((w0 & 1u) != 0u) out[n++] = static_cast<u16>(w0 >> 8u);
	}
	return n;
}

/// Valor del primer MOVE a `reg` (0xdead si no aparece).
u16 first_move_value(const u16* words, u16 count, u16 reg) {
	for (u16 i = 0; i + 1u < count; i += 2u) {
		const u16 w0 = words[i];
		if (w0 == 0xffffu) break;
		if ((w0 & 1u) != 0u) continue; // WAIT
		if (w0 == reg) return words[i + 1u];
	}
	return 0xdeadu;
}

eng::graphics::CopperIntent palette_intent(u16 line, const u16* color) {
	eng::graphics::CopperIntent it {};
	it.kind = eng::graphics::CopperIntentKind::PaletteLine;
	it.top = line;
	it.bottom = line;
	it.colors = eng::PaletteWords {color, 1};
	it.first = 0;
	it.count = 1;
	return it;
}

} // namespace

int main() {
	static const u16 kBase[4] = {0x000, 0x111, 0x222, 0x333};
	static const u16 kC1 = 0x0f0, kC2 = 0x00f, kC3 = 0xf00;

	MemorySystem mem = make_memory();
	eng::copper::Plan plan;
	if (!plan.begin(mem, {1024u})) {
		std::printf("[FAIL] Plan::begin fallo\n");
		return 1;
	}

	const u16 color_reg = static_cast<u16>(eng::copper::Register::COLOR00);

	// --- Frame 1: intenciones anadidas DESORDENADAS (100, 40, 70) ---------------
	plan.begin_frame();
	plan.scheduler().emit_palette(eng::PaletteWords {kBase, 4});
	plan.add(palette_intent(100u, &kC1));
	plan.add(palette_intent(40u, &kC2));
	plan.add(palette_intent(70u, &kC3));
	plan.materialize();
	u16* written = plan.inactive_words();
	u16* previous_active = plan.active_words();
	if (!plan.end_frame()) {
		std::printf("[FAIL] end_frame() devolvio false sin overflow\n");
		return 1;
	}
	// 2) end_frame voltea: lo escrito pasa a ser el bloque activo.
	if (plan.active_words() != written || plan.active_words() == previous_active) {
		std::printf("[FAIL] end_frame no volteo el double buffer\n");
		return 1;
	}
	// 3) Orden por scanline: los WAITs de las 3 intenciones salen 40, 70, 100.
	{
		u16 lines[8] = {0};
		const unsigned n = wait_lines(plan.active_words(), plan.words(), lines, 8);
		if (n < 3u || lines[0] != 40u || lines[1] != 70u || lines[2] != 100u) {
			std::printf("[FAIL] orden por scanline: n=%u [%u,%u,%u]\n", n,
				    (unsigned)lines[0], (unsigned)lines[1], (unsigned)lines[2]);
			return 1;
		}
	}
	// MOVEs a COLOR00: 1 de la paleta base + 1 por cada intencion (3).
	if (count_moves(plan.active_words(), plan.words(), color_reg) != 4u) {
		std::printf("[FAIL] COLOR00: %u (esperado 4 = base + 3 intenciones) words=%u\n",
			    count_moves(plan.active_words(), plan.words(), color_reg), (unsigned)plan.words());
		return 1;
	}
	// El bloque que NO se escribio sigue vacio (nunca se toca el activo).
	if (count_moves(plan.inactive_words(), plan.words(), color_reg) != 0u) {
		std::printf("[FAIL] se escribio en el bloque activo (no hay doble buffer)\n");
		return 1;
	}

	// --- Publicacion del bloque activo ------------------------------------------
	{
		MockBackend backend;
		plan.takeover(backend);
		plan.commit(backend);
		if (backend.taken != plan.active_words() || backend.installed != plan.active_words()) {
			std::printf("[FAIL] takeover/commit no publican el bloque activo\n");
			return 1;
		}
	}

	// --- Frame 2: escribe en el OTRO bloque y conserva el anterior --------------
	{
		static const u16 kBase2[4] = {0x0aa, 0x0bb, 0x0cc, 0x0dd};
		u16* frame1_active = plan.active_words();
		plan.begin_frame();
		plan.scheduler().emit_palette(eng::PaletteWords {kBase2, 4});
		plan.add(palette_intent(60u, &kC1));
		plan.materialize();
		if (!plan.end_frame()) {
			std::printf("[FAIL] end_frame() frame 2\n");
			return 1;
		}
		if (plan.active_words() == frame1_active) {
			std::printf("[FAIL] el frame 2 no uso el bloque nuevo\n");
			return 1;
		}
		// El bloque del frame 1 (ahora inactivo) conserva SU paleta base (0x000); el
		// nuevo trae la del frame 2 (0x0aa): no se ha tocado la lista anterior.
		if (first_move_value(plan.inactive_words(), plan.words(), color_reg) != 0x000u ||
		    first_move_value(plan.active_words(), plan.words(), color_reg) != 0x0aau) {
			std::printf("[FAIL] listas mezcladas: inactivo=0x%03x activo=0x%03x\n",
				    (unsigned)first_move_value(plan.inactive_words(), plan.words(), color_reg),
				    (unsigned)first_move_value(plan.active_words(), plan.words(), color_reg));
			return 1;
		}
	}

	// --- Overflow: no se publica una lista parcial -------------------------------
	{
		plan.begin_frame();
		for (unsigned i = 0; i < eng::copper::Plan::max_intents + 4u; ++i) {
			plan.add(palette_intent(static_cast<u16>(40u + i), &kC1));
		}
		plan.materialize();
		const bool ended = plan.end_frame();
		if (ended || !plan.overflow()) {
			std::printf("[FAIL] el overflow no se detecto (end_frame=%d overflow=%d)\n",
				    (int)ended, (int)plan.overflow());
			return 1;
		}
	}

	// --- attach: el plan puede orquestar un DoubleBuffer EXTERNO ------------------
	{
		static const u16 kAttach[4] = {0x077, 0x0bb, 0x0cc, 0x0dd};
		eng::copper::DoubleBuffer external;
		if (!external.begin(mem, 512u)) {
			std::printf("[FAIL] DoubleBuffer externo\n");
			return 1;
		}
		eng::copper::Plan attached;
		attached.attach(external);
		attached.begin_frame();
		attached.scheduler().emit_palette(eng::PaletteWords {kAttach, 4});
		attached.add(palette_intent(80u, &kC1));
		attached.materialize();
		if (!attached.end_frame()) {
			std::printf("[FAIL] end_frame del plan enlazado\n");
			return 1;
		}
		// Ha escrito en el buffer EXTERNO (su bloque activo es el del externo).
		if (attached.active_words() != external.active_words() ||
		    first_move_value(attached.active_words(), attached.words(), color_reg) != 0x077u) {
			std::printf("[FAIL] el plan enlazado no escribio en el buffer externo\n");
			return 1;
		}
	}

	// --- orden con CRUCE de las 256 lineas (ventana PAL que empieza en 0x2c) ------
	{
		eng::copper::Plan wrap;
		if (!wrap.begin(mem, {1024u, 0x2cu})) {
			std::printf("[FAIL] Plan::begin con first_line\n");
			return 1;
		}
		wrap.begin_frame();
		wrap.scheduler().emit_palette(eng::PaletteWords {kBase, 4});
		// top=250 va ANTES que top=10 (el raster llega a 250 y luego envuelve hasta 10).
		wrap.add(palette_intent(10u, &kC1));
		wrap.add(palette_intent(250u, &kC2));
		wrap.materialize();
		if (!wrap.end_frame()) {
			std::printf("[FAIL] end_frame del plan con cruce\n");
			return 1;
		}
		u16 lines[4] = {0};
		const unsigned n = wait_lines(wrap.active_words(), wrap.words(), lines, 4);
		if (n < 2u || lines[0] != 250u || lines[1] != 10u) {
			std::printf("[FAIL] orden con cruce: n=%u [%u,%u] (esperado 250,10)\n", n,
				    (unsigned)lines[0], (unsigned)lines[1]);
			return 1;
		}
	}

	std::printf("OK: copper::Plan (orden por scanline, doble buffer, publicacion y overflow).\n");
	return 0;
}
