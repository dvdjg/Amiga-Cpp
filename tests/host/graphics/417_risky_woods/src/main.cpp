// ============================================================================
// Test HOST-417: driver de fondo por sprites (effects::RiskyWoodsLayer).
// ============================================================================
//
// Valida en host (sin Amiga) la emision de `eng::effects::RiskyWoodsLayer`
// (`eng/api/effects.hpp`): el fondo por reposicion de `SPRxPOS` con carrera contra
// el haz.
//
// Comprobaciones:
//   1) `attach` rechaza geometria invalida y acepta la valida.
//   2) Por linea emite un `WAIT` por periodo y `channels` MOVEs de `SPRxPOS`.
//   3) Los canales ciclan `first..first+channels-1` y las X crecen `column_width` px.
//   4) Nunca reescribe `SPRxCTL` en la carrera (desarmaria el sprite).
//   5) El scroll desplaza las X y hace entrar/salir periodos.
//   6) `words_estimate` coincide con la huella real emitida.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/417_risky_woods   (solo este)
//   bash tools/run-host-tests.sh                                     (todos)

#include <cstdio>

#include <eng/api/effects.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

using eng::u8;
using eng::u16;
using eng::u32;
using eng::effects::RiskyWoodsLayer;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

/// Scheduler espia: captura WAITs y MOVEs (registro+valor) sin hardware.
struct SpySched {
	struct Mv { u16 reg; u16 val; };
	struct Wt { u16 line; u8 hpos; };
	Mv moves[512] {};
	Wt waits[256] {};
	unsigned n_moves = 0;
	unsigned n_waits = 0;

	void move(eng::copper::Register reg, u16 v) {
		if (n_moves < 512u) {
			moves[n_moves++] = Mv {static_cast<u16>(reg), v};
		}
	}
	void move(u16 reg, u16 v) {
		if (n_moves < 512u) {
			moves[n_moves++] = Mv {reg, v};
		}
	}
	/// MOVE parcheable: devuelve el índice (en words) de la instrucción.
	u16 move_at(eng::copper::Register reg, u16 v) {
		const u16 idx = static_cast<u16>(n_moves * 2u);
		move(reg, v);
		return idx;
	}
	u16 move_at(u16 reg, u16 v) {
		const u16 idx = static_cast<u16>(n_moves * 2u);
		move(reg, v);
		return idx;
	}
	[[nodiscard]] u16 words_used() const { return static_cast<u16>(n_moves * 2u); }
	void wait_line_safe(u16) {}
	void wait_position_safe(u16 line, u8 hpos) {
		if (n_waits < 256u) {
			waits[n_waits++] = Wt {line, hpos};
		}
	}
};

/// Config base para los tests: banda en 80..88, canales 2..7, patron de 6x16.
RiskyWoodsLayer::Config base_cfg(u16* dma) {
	RiskyWoodsLayer::Config c {};
	c.first_line = 80u;
	c.lines = 8u;
	c.channel_first = 2u;
	c.channels = 6u;
	c.column_width = 16u;
	c.screen_width = 320u;
	c.arm_hpos = 0x00u;
	c.head_start = 24u;
	c.dma_data = eng::Span<eng::u16> {dma, 64u};
	c.dma_stride = 4u;
	return c;
}

void test_attach_validation() {
	std::printf("RiskyWoodsLayer: validacion de attach\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	CHECK(l.attach(base_cfg(dma)));

	RiskyWoodsLayer::Config bad = base_cfg(dma);
	bad.channels = 0u;
	CHECK(!l.attach(bad));
	bad = base_cfg(dma);
	bad.channel_first = 4u;
	bad.channels = 6u; // 4..9 se sale de 0..7
	CHECK(!l.attach(bad));
	bad = base_cfg(dma);
	bad.dma_data = {};
	CHECK(!l.attach(bad));
	bad = base_cfg(dma);
	bad.lines = 0u;
	CHECK(!l.attach(bad));
}

void test_periods_and_channels() {
	std::printf("RiskyWoodsLayer: un WAIT por periodo y canales ciclando\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	CHECK(l.attach(c));

	SpySched s;
	l.emit_into(s);
	// Arranque: BPLCON2 (1) + por canal SPRxPTH/L + SPRxPOS + SPRxCTL (4).
	const unsigned boot = 1u + static_cast<unsigned>(c.channels) * 4u;
	// 320 px, 1.a col a 24 px, periodo 96 px -> periodos en 24, 120, 216, 312.
	// Columnas por periodo (se omiten las >= 320): 6+6+6+1 = 19 MOVEs y 4 WAITs.
	const unsigned per_line_moves = 19u;
	const unsigned per_line_waits = 4u;
	CHECK(s.n_moves == boot + per_line_moves * static_cast<unsigned>(c.lines));
	CHECK(s.n_waits == per_line_waits * static_cast<unsigned>(c.lines));

	// En la carrera (tras el arranque) solo se escribe SPRxPOS.
	for (unsigned i = boot; i < s.n_moves; ++i) {
		const u16 r = s.moves[i].reg;
		CHECK(r >= 0x140u && r <= 0x17eu);   // dentro de SPRxPOS
		CHECK(((r - 0x140u) % 8u) == 0u);    // es un POS, no CTL
	}
}

void test_pos_values_cycle() {
	std::printf("RiskyWoodsLayer: POS cicla canales y X crece 16 px\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	CHECK(l.attach(c));
	SpySched s;
	l.emit_into(s);
	const unsigned boot = 1u + static_cast<unsigned>(c.channels) * 4u;

	// Los 6 primeros POS de la 1.a linea: canales 2,3,4,5,6,7 y X 24,40,56,72,88,104.
	for (unsigned i = 0; i < 6u; ++i) {
		const u16 reg = s.moves[boot + i].reg;
		const u8 ch = static_cast<u8>((reg - 0x140u) / 8u);
		const u16 px = static_cast<u16>((s.moves[boot + i].val & 0xffu) * 2u);
		CHECK(ch == static_cast<u8>(2u + i));
		CHECK(px == static_cast<u16>(24u + i * 16u));
	}
}

void test_attach_sets_odd_bit() {
	std::printf("RiskyWoodsLayer: attach marca el bit 7 del canal impar\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	c.attach = true;
	CHECK(l.attach(c));
	SpySched s;
	l.emit_into(s);
	// Arranque: BPLCON2 + por canal (PTH, PTL, POS, CTL). El CTL del impar lleva bit 7.
	for (unsigned i = 0; i < c.channels; ++i) {
		const unsigned base = 1u + i * 4u;
		const u16 pos_reg = s.moves[base + 2u].reg;
		const u8 ch = static_cast<u8>((pos_reg - 0x140u) / 8u);
		CHECK(s.moves[base + 3u].reg == static_cast<u16>(0x142u + ch * 8u));
		CHECK(((s.moves[base + 3u].val & 0x80u) != 0u) == ((ch & 1u) != 0u));
	}
}

void test_burst_no_wait() {
	std::printf("RiskyWoodsLayer: burst_no_wait = un solo WAIT por linea\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	c.burst_no_wait = true;
	CHECK(l.attach(c));
	SpySched s;
	l.emit_into(s);
	// Sin WAITs intermedios: solo el `WAIT` inicial de cada linea.
	CHECK(s.n_waits == static_cast<unsigned>(c.lines));
}

void test_ctl_not_rewritten_in_carousel() {
	std::printf("RiskyWoodsLayer: la carrera no reescribe SPRxCTL\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	CHECK(l.attach(c));
	SpySched s;
	l.emit_into(s);
	const unsigned boot = 1u + static_cast<unsigned>(c.channels) * 4u;
	unsigned ctl_after_boot = 0u;
	for (unsigned i = boot; i < s.n_moves; ++i) {
		const u16 r = s.moves[i].reg;
		if ((r & 0x0fu) == 0x2u && (r & 0x1f0u) == 0x140u) {
			++ctl_after_boot;
		}
	}
	CHECK(ctl_after_boot == 0u);
}

void test_scroll_shifts_and_clips() {
	std::printf("RiskyWoodsLayer: el scroll desplaza las X y recorta\n");
	u16 dma[64] {};
	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	CHECK(l.attach(c));
	const unsigned boot = 1u + static_cast<unsigned>(c.channels) * 4u;

	// Con scroll=24, la 1.a columna (24 px) queda en 0.
	SpySched s1;
	l.set_scroll(24u);
	l.emit_into(s1);
	CHECK(s1.moves[boot].reg == 0x140u + 2u * 8u);          // canal 2
	CHECK((s1.moves[boot].val & 0xffu) * 2u == 0u);         // X=0 tras scroll

	// Con scroll=96 el primer periodo sale por la izquierda y entran mas por la derecha.
	SpySched s2;
	l.set_scroll(96u);
	l.emit_into(s2);
	CHECK(((s2.moves[boot].val & 0xffu) * 2u) < 16u);       // 1.a columna visible cerca de 0
}

void test_words_estimate() {
	std::printf("RiskyWoodsLayer: words_estimate = huella real\n");
	u16 dma[64] {};
	alignas(16) static eng::u8 chip[8 * 1024];
	eng::MemoryManager mem;
	mem.configure(chip, sizeof(chip), nullptr, 0u, nullptr, 0u, 16u);
	auto blk = mem.chip().reserve<eng::CopperTag>(2048u, 16u);
	eng::copper::Scheduler sched {blk};

	RiskyWoodsLayer l;
	RiskyWoodsLayer::Config c = base_cfg(dma);
	CHECK(l.attach(c));
	l.emit_into(sched);
	sched.end();
	CHECK(sched.words_used() == l.words_estimate());
}

} // namespace

int main() {
	std::printf("Test HOST-417 risky_woods_layer\n");
	std::printf("===============================\n");

	test_attach_validation();
	test_periods_and_channels();
	test_burst_no_wait();
	test_pos_values_cycle();
	test_ctl_not_rewritten_in_carousel();
	test_attach_sets_odd_bit();
	test_scroll_shifts_and_clips();
	test_words_estimate();

	if (g_failures == 0) {
		std::printf("OK: driver de fondo Risky Woods validado (WAIT/periodo, ciclo, scroll).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
