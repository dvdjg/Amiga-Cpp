// ============================================================================
// Test HOST-436: bending de un sprite por línea (`drivers::emit_sprite_bend_into`).
// ============================================================================
//
// Respalda `eng/graphics/drivers/sprite_bend.hpp` (F4.2): por cada línea del sprite se
// emite un WAIT + `SPRxPOS`/`SPRxCTL` con `VSTART = línea actual` (el registro rearma el
// DMA: `../WinUAE-DBG/custom.cpp:4078-4090`) y la X de una **tabla de offsets** potencia
// de dos. El `SPRxPT` no se toca; la DATA debe traer terminador `0,0` (anti-*Phantom
// Sprite*, `winuae/sprite-dma.md`).
//
// Comprobaciones:
//   1) Una línea = un WAIT + POS/CTL; X = x_base + offsets[(phase+i)&mask]; VSTOP = fin.
//   2) `phase` desplaza el índice con máscara; `step = 2` reusa el offset 2 líneas.
//   3) X recortada a >= 0; rechazos sin emisión: canal inválido, tabla no potencia de dos,
//      tabla vacía/1, DATA sin filas+terminador, altura 0.
//   4) Recorte de altura a `kSpriteMaxHeightLines` (128 líneas emitidas).
//   5) Varios canales **intercalados por línea** (un solo WAIT compartido; POS ch2 antes
//      que POS ch3 en la misma línea) — emitirlos por bloques separados desordena los WAIT.
//
// El scheduler se sustituye por el real ligado a un bloque Chip del arena (como HOST-428).
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/436_sprite_bend   (solo este)
//   bash tools/run-host-tests.sh                                       (todos)

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/sprite_bend.hpp>
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

using eng::graphics::drivers::emit_sprite_bend_into;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

alignas(16) eng::u8 g_chip[8 * 1024];

eng::MemoryManager make_memory() {
	eng::MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	return mem;
}

eng::copper::Scheduler make_scheduler(eng::MemoryManager& mem, eng::u32 words) {
	return eng::copper::Scheduler {mem.chip().reserve<eng::CopperTag>(words * 2u, 16u)};
}

/// Vista Chip sobre un array local (en host el banco es solo tipo).
template <eng::usize N>
eng::ChipView<eng::SpriteTag> chip(eng::u16 (&arr)[N]) {
	return eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(arr), N * 2u};
}

struct Mv {
	eng::u16 reg;
	eng::u16 val;
};

unsigned collect_moves(const eng::u16* w, eng::u16 count, Mv* out, unsigned max) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count && n < max; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) continue; // WAIT
		out[n++] = Mv {w[i], w[i + 1u]};
	}
	return n;
}

unsigned count_wait_line(const eng::u16* w, eng::u16 count, eng::u16 line) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u && (w[i] >> 8u) == line) ++n;
	}
	return n;
}

/// POS de la línea `line` en la copperlist (última escritura), o `0xffff` si no está.
eng::u16 pos_of(const eng::u16* w, eng::u16 count, eng::u16 channel, eng::u16 line) {
	eng::u16 pos = 0xffffu;
	eng::u16 cur_line = 0xffffu;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) {
			cur_line = static_cast<eng::u16>(w[i] >> 8u);
			continue;
		}
		if (w[i] == static_cast<eng::u16>(0x140u + channel * 8u) && cur_line == line) {
			pos = w[i + 1u];
		}
	}
	return pos;
}

void test_basic() {
	std::printf("bend: una linea = WAIT + POS/CTL con la X de la tabla\n");

	eng::u16 data[(4u * 2u + 2u)] {}; // 4 filas + terminador
	const eng::s16 table[4] = {0, 10, 0, -10};

	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 128u);
	emit_sprite_bend_into(sched, 2u, chip(data), 40u, 4u, 100u,
			      eng::Span<const eng::s16> {table, 4u}, 0u, 1u);
	sched.end();

	const eng::u16* w = sched.data();
	const eng::u16 count = sched.words_used();
	for (eng::u16 i = 0u; i < 4u; ++i) {
		CHECK(count_wait_line(w, count, static_cast<eng::u16>(40u + i)) == 1u);
	}
	CHECK(pos_of(w, count, 2u, 40u) == static_cast<eng::u16>((40u << 8u) | (100u >> 1u)));
	CHECK(pos_of(w, count, 2u, 41u) == static_cast<eng::u16>((41u << 8u) | (110u >> 1u)));
	CHECK(pos_of(w, count, 2u, 42u) == static_cast<eng::u16>((42u << 8u) | (100u >> 1u)));
	CHECK(pos_of(w, count, 2u, 43u) == static_cast<eng::u16>((43u << 8u) | (90u >> 1u)));

	// CTL del canal 2: uno por línea, VSTOP = 44 (fin exclusivo) en todos.
	unsigned ctls = 0u;
	eng::u16 last_ctl = 0u;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) == 0u && w[i] == 0x152u) {
			++ctls;
			last_ctl = w[i + 1u];
		}
	}
	CHECK(ctls == 4u);
	CHECK((last_ctl >> 8u) == 44u); // VSTOP = base + alto (exclusivo)
}

void test_phase_step_clamp() {
	std::printf("bend: fase, step y recorte de X\n");

	eng::u16 data[8u * 2u + 2u] {};
	const eng::s16 table[4] = {0, 10, 0, -10};

	// phase = 3: indices 3,0,1,2 -> X 90, 100, 110, 100.
	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 128u);
	emit_sprite_bend_into(sched, 0u, chip(data), 50u, 4u, 100u,
			      eng::Span<const eng::s16> {table, 4u}, 3u, 1u);
	sched.end();
	CHECK(pos_of(sched.data(), sched.words_used(), 0u, 50u) ==
	      static_cast<eng::u16>((50u << 8u) | (90u >> 1u)));
	CHECK(pos_of(sched.data(), sched.words_used(), 0u, 51u) ==
	      static_cast<eng::u16>((51u << 8u) | (100u >> 1u)));

	// step = 2: un offset por cada 2 lineas.
	eng::MemoryManager mem2 = make_memory();
	eng::copper::Scheduler sched2 = make_scheduler(mem2, 128u);
	emit_sprite_bend_into(sched2, 0u, chip(data), 60u, 4u, 200u,
			      eng::Span<const eng::s16> {table, 4u}, 0u, 2u);
	sched2.end();
	CHECK(pos_of(sched2.data(), sched2.words_used(), 0u, 60u) ==
	      static_cast<eng::u16>((60u << 8u) | (200u >> 1u)));
	CHECK(pos_of(sched2.data(), sched2.words_used(), 0u, 61u) ==
	      static_cast<eng::u16>((61u << 8u) | (200u >> 1u))); // mismo offset
	CHECK(pos_of(sched2.data(), sched2.words_used(), 0u, 62u) ==
	      static_cast<eng::u16>((62u << 8u) | (210u >> 1u)));

	// Recorte a >= 0: offset -200 con base 100 -> X 0.
	const eng::s16 neg[2] = {-200, 0};
	eng::MemoryManager mem3 = make_memory();
	eng::copper::Scheduler sched3 = make_scheduler(mem3, 128u);
	emit_sprite_bend_into(sched3, 0u, chip(data), 70u, 2u, 100u,
			      eng::Span<const eng::s16> {neg, 2u}, 0u, 1u);
	sched3.end();
	CHECK(pos_of(sched3.data(), sched3.words_used(), 0u, 70u) ==
	      static_cast<eng::u16>((70u << 8u) | 0u));
}

void test_multi_interleaved() {
	std::printf("bend: varios canales intercalados por linea (un WAIT compartido)\n");

	eng::u16 data[2u * 2u + 2u] {}; // 2 filas + terminador
	const eng::s16 table[4] = {0, 10, 0, -10};
	eng::graphics::drivers::SpriteBend bends[2] = {
		{2u, chip(data), 100u, 0u},
		{3u, chip(data), 200u, 1u},
	};

	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 64u);
	emit_sprite_bend_into(sched, eng::Span<const eng::graphics::drivers::SpriteBend> {bends, 2u},
			      50u, 2u, eng::Span<const eng::s16> {table, 4u}, 1u);
	sched.end();

	const eng::u16* w = sched.data();
	const eng::u16 count = sched.words_used();
	// Un WAIT por línea (no dos) y ambos canales presentes por línea.
	CHECK(count_wait_line(w, count, 50u) == 1u);
	CHECK(count_wait_line(w, count, 51u) == 1u);
	CHECK(pos_of(w, count, 2u, 50u) == static_cast<eng::u16>((50u << 8u) | (100u >> 1u)));
	CHECK(pos_of(w, count, 3u, 50u) == static_cast<eng::u16>((50u << 8u) | (210u >> 1u)));
	// Orden: en la línea 50, POS ch2 antes que POS ch3, y ambos tras el WAIT 50.
	int pos2 = -1, pos3 = -1, wait50 = -1, wait51 = -1;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) {
			if ((w[i] >> 8u) == 50u && wait50 < 0) wait50 = static_cast<int>(i);
			if ((w[i] >> 8u) == 51u && wait51 < 0) wait51 = static_cast<int>(i);
			continue;
		}
		if (w[i] == 0x150u && pos2 < 0) pos2 = static_cast<int>(i); // SPR2POS
		if (w[i] == 0x158u && pos3 < 0) pos3 = static_cast<int>(i); // SPR3POS
	}
	CHECK(wait50 >= 0 && pos2 > wait50 && pos3 > pos2 && wait51 > pos3);
}

void test_rejects() {
	std::printf("bend: rechazos sin emision\n");

	eng::u16 data[8u * 2u + 2u] {};
	const eng::s16 table[4] = {0, 10, 0, -10};
	const eng::s16 table3[3] = {0, 10, 0};
	const eng::s16 one[1] = {5};

	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 128u);
	emit_sprite_bend_into(sched, 8u, chip(data), 40u, 4u, 100u,
			      eng::Span<const eng::s16> {table, 4u}, 0u, 1u); // canal invalido
	emit_sprite_bend_into(sched, 0u, chip(data), 40u, 4u, 100u,
			      eng::Span<const eng::s16> {table3, 3u}, 0u, 1u); // no potencia de dos
	emit_sprite_bend_into(sched, 0u, chip(data), 40u, 4u, 100u,
			      eng::Span<const eng::s16> {one, 1u}, 0u, 1u);    // tabla trivial
	emit_sprite_bend_into(sched, 0u, chip(data), 40u, 0u, 100u,
			      eng::Span<const eng::s16> {table, 4u}, 0u, 1u);  // altura 0
	// DATA sin terminador: 8 filas pedidas, el array tiene 8*2+2 = 18 words; pide 16*... con
	// altura 9 se exigen 9*2+2 = 20 words -> rechazo.
	emit_sprite_bend_into(sched, 0u, chip(data), 40u, 9u, 100u,
			      eng::Span<const eng::s16> {table, 4u}, 0u, 1u);
	sched.end();

	Mv mv[8] {};
	CHECK(collect_moves(sched.data(), sched.words_used(), mv, 8u) == 0u);
}

void test_height_clamp() {
	std::printf("bend: altura recortada a 128 lineas\n");

	eng::u16 data[130u * 2u + 2u] {};
	const eng::s16 table[2] = {0, 0};

	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 1024u);
	emit_sprite_bend_into(sched, 1u, chip(data), 10u, 130u, 100u,
			      eng::Span<const eng::s16> {table, 2u}, 0u, 1u);
	sched.end();

	unsigned total = 0u;
	eng::u16 last_line = 0u;
	for (eng::u16 line = 10u; line < 200u; ++line) {
		if (count_wait_line(sched.data(), sched.words_used(), line) != 0u) {
			++total;
			last_line = line;
		}
	}
	CHECK(total == 128u);
	CHECK(last_line == 137u); // 10 + 128 - 1
}

} // namespace

int main() {
	std::printf("Test HOST-436 sprite_bend (bending por linea, F4.2)\n");
	std::printf("====================================================\n");

	test_basic();
	test_phase_step_clamp();
	test_multi_interleaved();
	test_rejects();
	test_height_clamp();

	if (g_failures == 0) {
		std::printf("OK: bending por linea validado.\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
