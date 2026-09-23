// ============================================================================
// Test HOST-260: Copper lanza blits (Tecnica A) — `CopperIntentKind::BlitterJob`.
// ============================================================================
//
// Valida en host (sin Amiga):
//   1) El `CopperIntent` de tipo `BlitterJob` programa los registros del Blitter y escribe
//      `BLTSIZE` AL FINAL (arranca el blit).
//   2) La **ventana segura** (`Scheduler::set_blitter_window`) gate: mismo intent dentro de
//      la ventana se emite; fuera se cuenta como `unhandled_intents` y no escribe registros.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/260_copper_blitter

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u8;
using eng::u16;
using eng::u32;

alignas(16) eng::u8 g_chip[16 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

eng::copper::Scheduler make_scheduler(MemorySystem& mem, u32 words) {
	return eng::copper::Scheduler {mem.chip.allocate_block<eng::CopperTag>(words * 2u, 16u)};
}

unsigned g_fail = 0;
#define CHECK(cond, msg)                                                          \
	do {                                                                      \
		if (!(cond)) {                                                    \
			std::printf("FAIL: %s (linea %d)\n", msg, __LINE__);      \
			++g_fail;                                                 \
		}                                                                 \
	} while (0)

struct Mv {
	u16 reg;
	u16 val;
};
unsigned collect_moves(const u16* w, u16 count, Mv* out, unsigned max) {
	unsigned n = 0;
	for (u16 i = 0; i + 1u < count && n < max; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) continue; // WAIT
		out[n++] = Mv {w[i], w[i + 1u]};
	}
	return n;
}
unsigned count_waits(const u16* w, u16 count) {
	unsigned n = 0;
	for (u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) ++n;
	}
	return n;
}

eng::graphics::BlitterJob make_job() {
	eng::graphics::BlitterJob job {};
	job.bltcon0 = 0x09f0u; // USEA|USED|minterm $F0 (D = A)
	job.bltcon1 = 0x0000u;
	job.bltamod = 0;
	job.bltdmod = 0;
	job.bltapt = reinterpret_cast<const void*>(0x00001000u);
	job.bltdpt = reinterpret_cast<void*>(0x00002000u);
	job.bltsize = static_cast<u16>((1u << 6u) | 1u);
	return job;
}

void test_in_window() {
	MemorySystem mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);
	const eng::graphics::BlitterJob job = make_job();
	eng::graphics::CopperIntent it {};
	it.kind = eng::graphics::CopperIntentKind::BlitterJob;
	it.top = 200u;
	it.blitter_job = &job;

	sched.set_blitter_window(eng::graphics::BlitterWindow {200u, 210u});
	sched.emit_copper_intents(&it, 1u);
	sched.end();

	const u16* w = sched.data();
	Mv mv[32] {};
	const unsigned nm = collect_moves(w, sched.words_used(), mv, 32u);

	CHECK(nm == 13u, "BlitterJob emite 8 regs + A(2) + D(2) + BLTSIZE = 13 MOVEs");
	CHECK(nm > 0u && mv[0].reg == static_cast<u16>(eng::copper::Register::BLTCON0),
	      "primer MOVE = BLTCON0");
	CHECK(nm > 0u && mv[nm - 1u].reg == static_cast<u16>(eng::copper::Register::BLTSIZE),
	      "BLTSIZE es el ULTIMO MOVE (arranca el blit)");
	CHECK(count_waits(w, sched.words_used()) == 1u, "un WAIT a la linea del job");
	CHECK(sched.report().unhandled_intents == 0u, "dentro de la ventana: manejado");
}

void test_out_of_window() {
	MemorySystem mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);
	const eng::graphics::BlitterJob job = make_job();
	eng::graphics::CopperIntent it {};
	it.kind = eng::graphics::CopperIntentKind::BlitterJob;
	it.top = 100u; // dentro del area visible (NO seguro)
	it.blitter_job = &job;

	sched.set_blitter_window(eng::graphics::BlitterWindow {300u, 310u});
	sched.emit_copper_intents(&it, 1u);
	sched.end();

	Mv mv[32] {};
	const unsigned nm = collect_moves(sched.data(), sched.words_used(), mv, 32u);
	CHECK(nm == 0u, "fuera de la ventana no emite registros");
	CHECK(sched.report().unhandled_intents == 1u, "fuera de la ventana: no manejado");
}

void test_null_job() {
	MemorySystem mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 64u);
	eng::graphics::CopperIntent it {};
	it.kind = eng::graphics::CopperIntentKind::BlitterJob;
	it.top = 300u;
	it.blitter_job = nullptr;
	sched.emit_copper_intents(&it, 1u);
	sched.end();
	CHECK(sched.report().unhandled_intents == 1u, "job nulo: no manejado");
}

/// Modelo de coste del Blitter y **ventana segura automática** (el blit del Copper debe caer
/// después de los blits de CPU; el Blitter es único).
void test_safe_window() {
	CHECK(eng::graphics::blitter_lines(0u) == 0u, "blitter_lines(0) = 0");
	CHECK(eng::graphics::blitter_lines(227u) == 6u, "blitter_lines(227) = 6");
	CHECK(eng::graphics::blitter_lines(5100u) == 134u, "blitter_lines(5100) = 134");

	// CPU acaba antes del borde -> gana el borde.
	const eng::graphics::BlitterWindow w1 =
		eng::graphics::safe_blitter_window(5100u, 0x2cu, 0x130u, 0x138u);
	CHECK(w1.first == 0x130u && w1.last == 0x138u,
	      "ventana: gana el borde si el CPU acaba antes");

	// CPU acaba DESPUÉS del borde -> la ventana empieza tras el (evita abortarlo).
	// cpu_start 250 + 4000 words (105 líneas) = 355; last 360 -> {355, 360}.
	const eng::graphics::BlitterWindow w2 =
		eng::graphics::safe_blitter_window(4000u, 250u, 0x130u, 360u);
	CHECK(w2.first == 355u && w2.last == 360u,
	      "ventana: empieza tras el blit de CPU si lo sobrepasa");

	// CPU que no cabe en el frame -> se recorta a `last`.
	const eng::graphics::BlitterWindow w3 =
		eng::graphics::safe_blitter_window(60000u, 44u, 0x130u, 310u);
	CHECK(w3.first == 310u && w3.last == 310u, "ventana: se recorta a last si el CPU desborda");

	// Contrato negativo (documentado): una ventana que empieza ANTES del fin del CPU deja que
	// el BLTSIZE del Copper caiga mientras corre el de CPU -> el Blitter único lo aborta.
	CHECK(0x130u < w2.first, "ventana insegura: 304 cae antes del fin del CPU (355)");
}

} // namespace

int main() {
	test_in_window();
	test_out_of_window();
	test_null_job();
	test_safe_window();
	if (g_fail == 0u) {
		std::printf("OK: Copper lanza blits (BlitterJob + ventana segura).\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
