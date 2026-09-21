// ============================================================================
// Test HOST-105: rearmado horizontal de sprite (multiplexado por linea)
// ============================================================================
//
// Valida en host (sin Amiga) `Scheduler::emit_sprite_horizontal_rearm(s)`:
//
//   1) Codificacion AHRM de SPRxPOS/SPRxCTL (VSTART/VSTOP/HSTART y attach).
//   2) La secuencia por rearm es WAIT(posicion) + POS + CTL + DATA + DATB, y
//      NO toca SPRxPT (reasignarlo relanzaria la secuencia DMA).
//   3) La variante de lista respeta el orden por `hpos` (ignora los atrasados:
//      un WAIT ya pasado esperaria al frame siguiente).
//   4) Los offsets de registro son los del canal (`0x140 + ch*8`, etc.).
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/105_sprite_hrearm

#include <cstdio>

#include <eng/api/effects.hpp>
#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/graphics/sprite_collision.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u8;
using eng::u16;
using eng::u32;
using eng::graphics::SpriteHorizontalRearm;

alignas(16) eng::u8 g_chip[16 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

/// `Scheduler` ligado a un bloque de Chip RAM del arena.
eng::copper::Scheduler make_scheduler(MemorySystem& mem, u32 words) {
	return eng::copper::Scheduler {
		mem.chip.allocate_block<eng::CopperTag>(words * 2u, 16u)
	};
}

unsigned g_fail = 0;
#define CHECK(cond, msg)                                                          \
	do {                                                                      \
		if (!(cond)) {                                                    \
			std::printf("FAIL: %s (linea %d)\n", msg, __LINE__);      \
			++g_fail;                                                 \
		}                                                                 \
	} while (0)

/// Un MOVE es `w0` par (registro) seguido de `w1` (dato). Devuelve cuantos aparecen.
struct Mv {
	u16 reg;
	u16 val;
};
unsigned collect_moves(const u16* w, u16 count, Mv* out, unsigned max) {
	unsigned n = 0;
	for (u16 i = 0; i + 1u < count && n < max; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) continue; // WAIT (bit 0 = 1)
		out[n++] = Mv {w[i], w[i + 1u]};
	}
	return n;
}

/// ¿Aparece un WAIT con posicion distinta a `line_hi` (cualquier H)?
bool has_wait_at(const u16* w, u16 count, u8 vpos) {
	for (u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u && static_cast<u8>((w[i] >> 8u) & 0xffu) == vpos) {
			return true;
		}
	}
	return false;
}

void test_encoding() {
	MemorySystem mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);

	// Canal 3, VSTART=0x2c, VSTOP=0x31, hpos=0x46 (low-res px), attach.
	SpriteHorizontalRearm r {};
	r.channel = 3u;
	r.vstart = 0x2cu;
	r.vstop = 0x31u;
	r.hpos = 0x46u;
	r.data_high = 0x1234u;
	r.data_low = 0x5678u;
	r.attach = true;
	sched.emit_sprite_horizontal_rearm(r);
	sched.end();

	const u16* w = sched.data();
	const u16 count = sched.words_used();
	Mv mv[8] {};
	const unsigned nm = collect_moves(w, count, mv, 8u);

	// POS: (VSTART[7:0]<<8) | (hpos>>1). CTL (AHRM): VSTOP[7:0]<<8 | ATTACH(bit7) |
	// VSTART[8](bit2) | VSTOP[8](bit1) | HSTART[0](bit0).
	const u16 pos_expected = static_cast<u16>((0x2cu << 8u) | ((0x46u >> 1u) & 0xffu));
	const u16 ctl_expected = static_cast<u16>((0x31u << 8u) | 0x0080u);

	CHECK(nm == 4u, "el rearm emite exactamente 4 MOVEs");
	if (nm == 4u) {
		CHECK(mv[0].reg == 0x140u + 3u * 8u, "SPRxPOS usa el offset del canal");
		CHECK(mv[0].val == pos_expected, "SPRxPOS codifica VSTART/HSTART");
		CHECK(mv[1].reg == 0x142u + 3u * 8u, "SPRxCTL usa el offset del canal");
		CHECK(mv[1].val == ctl_expected, "SPRxCTL codifica VSTOP/HSTART-par/attach");
		CHECK(mv[2].reg == 0x144u + 3u * 8u && mv[2].val == 0x1234u, "SPRxDATA (arma)");
		CHECK(mv[3].reg == 0x146u + 3u * 8u && mv[3].val == 0x5678u, "SPRxDATB");
	}
	// NO debe tocar SPRxPT (0x120..0x122 del canal).
	bool touches_pt = false;
	for (unsigned i = 0; i < nm; ++i) {
		if (mv[i].reg == 0x120u + 3u * 4u || mv[i].reg == 0x122u + 3u * 4u) {
			touches_pt = true;
		}
	}
	CHECK(!touches_pt, "el rearm horizontal NO toca SPRxPT");
	CHECK(has_wait_at(w, count, 0x2cu), "hay WAIT a la linea VSTART");

	// El WAIT debe tener posicion horizontal (no esperar solo por V): comprobamos que
	// su word0 lleva H != 1 (el WAIT de linea simple usa hpos=1).
	bool wait_has_h = false;
	for (u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) {
			const u16 hv = static_cast<u16>(w[i] & 0xffu);
			if (hv > 1u) {
				wait_has_h = true;
			}
		}
	}
	CHECK(wait_has_h, "el WAIT del rearm espera a una posicion H concreta");
}

void test_list_order() {
	MemorySystem mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);

	SpriteHorizontalRearm list[3] {};
	list[0].channel = 0u;
	list[0].vstart = 0x40u;
	list[0].vstop = 0x41u;
	list[0].hpos = 0x20u;
	list[0].data_high = 0xaaaa;
	list[1].channel = 1u;
	list[1].vstart = 0x40u;
	list[1].vstop = 0x41u;
	list[1].hpos = 0x10u; // ATRASADO: <= anterior -> debe ignorarse
	list[1].data_high = 0xbbbb;
	list[2].channel = 2u;
	list[2].vstart = 0x40u;
	list[2].vstop = 0x41u;
	list[2].hpos = 0x38u;
	list[2].data_high = 0xcccc;
	sched.emit_sprite_horizontal_rearms(list, 3u);
	sched.end();

	Mv mv[16] {};
	const unsigned nm = collect_moves(sched.data(), sched.words_used(), mv, 16u);
	// 2 rearms validos * 4 MOVEs = 8; el atrasado se ignora.
	CHECK(nm == 8u, "la lista ignora el rearm con hpos atrasado");
	bool saw_ch1 = false;
	for (unsigned i = 0; i < nm; ++i) {
		if (mv[i].reg == 0x140u + 1u * 8u) {
			saw_ch1 = true;
		}
	}
	CHECK(!saw_ch1, "el canal del rearm atrasado no se programa");
}

/// `SpriteManager` emite el bit ATTACH (0) de `SPRxCTL` cuando el sprite va attached.
void test_attach() {
	MemorySystem mem = make_memory();
	eng::graphics::SpriteManager sm;
	CHECK(sm.init(mem, 128u), "SpriteManager init");
	const eng::Span<u8> data = sm.sprite_data();
	eng::graphics::SpriteConfig cfg {};
	cfg.enabled = true;
	cfg.data = eng::Span<const eng::u16> {reinterpret_cast<const eng::u16*>(data.data()), 4u};
	cfg.width_words = 1;
	cfg.height = 1;
	cfg.hpos = 20;
	cfg.vstart = 10;
	cfg.vstop = 10;
	cfg.attach = true;
	sm.set(1u, cfg);

	eng::copper::Scheduler s = make_scheduler(mem, 64u);
	sm.emit_into(s);
	s.end();
	Mv mv[32] {};
	const unsigned n = collect_moves(s.data(), s.words_used(), mv, 32u);
	bool found = false;
	for (unsigned i = 0; i < n; ++i) {
		if (mv[i].reg == 0x142u + 1u * 8u) {
			found = true;
			CHECK((mv[i].val & 0x0080u) != 0u, "SPR1CTL lleva el bit ATTACH (bit 7)");
		}
	}
	CHECK(found, "SPR1CTL emitido por SpriteManager");
}

/// Colision de hardware: codificacion de CLXCON y decodificacion de CLXDAT (AHRM 7-3/7-4).
void test_collision() {
	using eng::graphics::decode_clxdat;
	using eng::graphics::encode_clxcon;
	using eng::graphics::SpriteCollisionConfig;
	CHECK(encode_clxcon(SpriteCollisionConfig {0x1u, 0x01u, 0x01u}) == 0x1041u,
	      "CLXCON: ENSP1 + ENBP1 + MVBP1");
	CHECK(encode_clxcon(SpriteCollisionConfig {0xFu, 0x3Fu, 0x3Fu}) == 0xFFFFu,
	      "CLXCON: todo habilitado = 0xFFFF");
	const auto r = decode_clxdat(static_cast<u16>((1u << 0) | (1u << 9)));
	CHECK(r.even_vs_odd_bitplanes(), "CLXDAT bit 0 (pares vs impares)");
	CHECK(r.sprite_vs_sprite(0u, 1u), "CLXDAT sprite 0/1 vs 2/3");
	CHECK(!r.sprite_vs_sprite(1u, 2u), "CLXDAT sin 2/3 vs 4/5");
	CHECK(r.sprite_vs_sprite(1u, 0u), "CLXDAT par desordenado se normaliza");
	const auto r2 = decode_clxdat(static_cast<u16>((1u << 1) | (1u << 7)));
	CHECK(r2.odd_bpl_vs_sprite(0u), "CLXDAT impares vs 0/1");
	CHECK(r2.even_bpl_vs_sprite(2u), "CLXDAT pares vs 4/5");
}

/// `effects::SpriteLayer`: un WAIT por linea + rafaga POS/DATB/DATA, prioridad de fondo.
void test_sprite_layer() {
	struct FakeSched {
		int waits = 0;
		int moves = 0;
		eng::u16 bplcon2 = 0xffffu;
		void move(eng::copper::Register reg, eng::u16 v) {
			if (reg == eng::copper::Register::BPLCON2) {
				bplcon2 = v;
			}
			++moves;
		}
		void wait_line_safe(eng::u16) { ++waits; }
	};
	eng::effects::SpriteLayer layer;
	eng::effects::SpriteLayer::Config cfg {};
	cfg.first_line = 100u;
	cfg.lines = 4u;
	cfg.channels = 8u;
	cfg.hpos0 = 64u;
	cfg.hpos_step = 16u;
	cfg.bplcon2 = 0x0008u;
	CHECK(layer.attach(cfg), "SpriteLayer attach");
	FakeSched fs;
	layer.emit_into(fs);
	CHECK(fs.waits == 4, "SpriteLayer: 4 lineas -> 4 WAIT");
	CHECK(fs.moves == 1 + 4 * 8 * 4, "SpriteLayer: BPLCON2 + 4 lineas*8 canales*4 MOVEs");
	CHECK(fs.bplcon2 == 0x0008u, "SpriteLayer: BPLCON2 de fondo");
	CHECK(layer.words_estimate() == 1u + 4u * (2u + 8u * 8u), "SpriteLayer: huella");

	eng::effects::SpriteLayer bad;
	eng::effects::SpriteLayer::Config bad_cfg {};
	bad_cfg.lines = 0u;
	bad_cfg.hpos_step = 16u;
	CHECK(!bad.attach(bad_cfg), "SpriteLayer: lines=0 -> false");

	// Canales DMA: SPRxPT a la estructura (cabecera POS+CTL en memoria); sin rearm por linea.
	eng::u16 dma_col[16] {};
	eng::effects::SpriteLayer dma;
	eng::effects::SpriteLayer::Config dcfg {};
	dcfg.first_line = 100u;
	dcfg.lines = 4u;
	dcfg.channels = 8u;
	dcfg.hpos_step = 16u;
	dcfg.dma_channels = 2u;
	dcfg.dma_stride = 8u;
	dcfg.dma_data = dma_col;
	CHECK(dma.attach(dcfg), "SpriteLayer DMA attach");
	FakeSched fd;
	dma.emit_into(fd);
	// BPLCON2(1) + DMA 2*2 (PT H/L) + 4 lineas*(6 canales*4) = 1+4+96 = 101.
	CHECK(fd.moves == 1 + 4 + 4 * 6 * 4, "SpriteLayer DMA: MOVEs");
	CHECK(fd.waits == 4, "SpriteLayer DMA: 4 WAIT (solo canales Copper)");
	eng::effects::SpriteLayer bad2;
	eng::effects::SpriteLayer::Config b2 {};
	b2.lines = 1u;
	b2.hpos_step = 16u;
	b2.dma_channels = 2u; // sin dma_data
	CHECK(!bad2.attach(b2), "SpriteLayer: DMA sin data -> false");
}

} // namespace

int main() {
	test_encoding();
	test_list_order();
	test_attach();
	test_collision();
	test_sprite_layer();
	if (g_fail == 0u) {
		std::printf("OK: sprite horizontal rearm (codificacion, secuencia, orden de lista)\n");
		return 0;
	}
	std::printf("FALLOS: %u\n", g_fail);
	return 1;
}
