// ============================================================================
// Test HOST-434: emisión DMA encadenada por canal (F3, builder + parcheo).
// ============================================================================
//
// Valida en host el módulo puro `eng/graphics/sprite_chain.hpp`:
//   - `encode_sprite_pos_ctl`: codificación POS/CTL de una cabecera de sprite
//     (AHRM 3.ª cap. 4; layout verificado contra `../WinUAE-DBG/custom.cpp:4044-4058`);
//   - `build_sprite_dma_chain`: estructura `[POS,CTL,DAT/DATB…][POS,CTL,…]…[0,0]`
//     que Agnus recorre sola (el walk del DMA consume cada bloque: cabecera con
//     `dmastate == 0`, `custom.cpp:4078-4090`, 10055-10120 y 12012-12023; ver
//     `docs/reference/emulators/winuae/sprite-dma.md`);
//   - `patch_sprite_chain_block`: parcheo de POS/CTL (4 bytes) de la cadena persistente.
//
// La storage es `ChipView<SpriteTag>` (DMA tipada por banco, AGENTS §1.10); en host el
// banco es solo tipo y la construcción es CPU. La *golden* compara la secuencia de
// words **exacta** (no una propiedad): es el contrato del formato que lee Agnus.
//
// Comprobaciones:
//   1) Codificación: bits altos (VSTART[8]/VSTOP[8]/HSTART[0]), VSTOP exclusivo, ATTACH.
//   2) Golden de 2 bloques: words exactas, offsets de bloque, DATA byte a byte, terminador.
//   3) Ancho de 32 px (`width_words = 2`) y HSTART con bit bajo.
//   4) Rechazos sin escritura: vacío, gap/solape, canales mezclados, ATTACH en canal par,
//      altura 0, ancho 0, DATA corta, capacidad, tope de bloques.
//   5) Recorte de altura a 128 líneas (`kSpriteMaxHeightLines`).
//   6) Parcheo: solo cambia POS/CTL del bloque pedido; rechaza índice/paridad inválidos.
//   7) El builder NO toca el runway (lo que sigue al terminador queda como estaba).
//   8) `SpriteManager::arm_dma_chain` (solo PTH/PTL) y `emit_placements_into` con `chains`:
//      el canal con cadena se arma sin rearmes ni POS/CTL, el suelto sigue su camino y una
//      cadena que no cubre el canal degrada a rearme.
//   9) `choose_sprite_emit_plan` (F3.3): Rearm/Chain/ChainPersistent por canal, presupuesto
//      de DATA y coherencia de índice/canal de la cadena.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/434_sprite_dma_chain   (solo este)
//   bash tools/run-host-tests.sh                                           (todos)

#include <cstdio>

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite.hpp>
#include <eng/graphics/sprite_chain.hpp>
#include <eng/graphics/sprite_limits.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

using eng::graphics::build_sprite_dma_chain;
using eng::graphics::choose_sprite_emit_plan;
using eng::graphics::encode_sprite_pos_ctl;
using eng::graphics::HwSpritePlacement;
using eng::graphics::kSpriteChainMaxBlocks;
using eng::graphics::patch_sprite_chain_block;
using eng::graphics::SpriteChainBudget;
using eng::graphics::SpriteDmaChain;
using eng::graphics::SpriteEmitPlan;
using eng::graphics::SpriteEmitStrategy;
using eng::graphics::SpriteManager;
using eng::graphics::SpritePosCtl;

int g_failures = 0;

#define CHECK(cond)                                                       \
	do {                                                                  \
		if (!(cond)) {                                                     \
			std::printf("  [FAIL] %s (linea %d)\n", #cond, __LINE__);      \
			++g_failures;                                                  \
		}                                                                 \
	} while (0)

/// Vista Chip sobre un array local (en host el banco es solo tipo; el builder es CPU).
eng::ChipView<eng::SpriteTag> chip(eng::u16* p, eng::usize words) {
	return eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(p), words * 2u};
}

template <eng::usize N>
eng::ChipView<eng::SpriteTag> chip(eng::u16 (&arr)[N]) {
	return chip(arr, N);
}

HwSpritePlacement plc(eng::u8 channel, eng::u16 hpos, eng::u16 vstart, eng::u16 height,
		      eng::u8 width_words, bool attach, eng::ChipView<eng::SpriteTag> data) {
	HwSpritePlacement p {};
	p.channel = channel;
	p.hpos = hpos;
	p.vstart = vstart;
	p.height = height;
	p.width_words = width_words;
	p.attach = attach;
	p.data = data;
	return p;
}

void test_encode() {
	std::printf("encode_sprite_pos_ctl: formatos POS/CTL\n");
	// POS: VSTART en el byte alto, HSTART/2 en el bajo; CTL: VSTOP exclusivo, ATTACH, bit8s.
	SpritePosCtl e = encode_sprite_pos_ctl(0x80u, 0x30u, 3u, false);
	CHECK(e.pos == 0x3040u && e.ctl == 0x3300u);

	e = encode_sprite_pos_ctl(0x80u, 0x30u, 3u, true);
	CHECK(e.ctl == 0x3380u); // bit 7: ATTACH

	// vstart > 255: los bits 8 van al CTL (bit 2 VSTART[8], bit 1 VSTOP[8]); HSTART[0] (0x11 odd).
	e = encode_sprite_pos_ctl(0x11u, 0x1a5u, 1u, false);
	CHECK(e.pos == 0xa508u);
	CHECK(e.ctl == 0xa607u);

	// X par y byte alto de HSTART al completo (0x100 >> 1 = 0x80).
	e = encode_sprite_pos_ctl(0x100u, 0x20u, 4u, false);
	CHECK(e.pos == 0x2080u && e.ctl == 0x2400u);

	// HSTART[0] pasa tal cual (bit 0 del CTL).
	e = encode_sprite_pos_ctl(0x41u, 0x10u, 1u, false);
	CHECK(e.pos == 0x1020u && e.ctl == 0x1101u);
}

void test_golden() {
	std::printf("build: golden de 2 bloques (words exactas + terminador + offsets)\n");

	eng::u16 data_a[6] = {0x1111u, 0x2222u, 0x3333u, 0x4444u, 0x5555u, 0x6666u};
	eng::u16 data_b[4] = {0xaaaau, 0xbbbbu, 0xccccu, 0xddddu};

	HwSpritePlacement plcs[2] = {
		plc(3u, 0x40u, 0x30u, 3u, 1u, false, chip(data_a)),
		plc(3u, 0x60u, 0x34u, 2u, 1u, false, chip(data_b)),
	};

	eng::u16 storage[24];
	for (eng::u16& w : storage) {
		w = 0xbeefu; // sentinela: el runway no se toca
	}
	SpriteDmaChain chain {};
	chain.storage = chip(storage);

	const eng::u16 expect[16] = {
		0x3020u, 0x3300u, // bloque 1: POS VSTART=0x30/X=0x40, CTL VSTOP=0x33
		0x1111u, 0x2222u, 0x3333u, 0x4444u, 0x5555u, 0x6666u,
		0x3430u, 0x3600u, // bloque 2: VSTART=0x34/X=0x60, VSTOP=0x36
		0xaaaau, 0xbbbbu, 0xccccu, 0xddddu,
		0x0000u, 0x0000u, // terminador
	};
	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));
	CHECK(chain.used == 16u);
	CHECK(chain.block_count == 2u);
	CHECK(chain.channel == 3u);
	CHECK(chain.block_pos_words[0] == 0u && chain.block_pos_words[1] == 8u);
	for (eng::usize i = 0u; i < 16u; ++i) {
		if (storage[i] != expect[i]) {
			std::printf("  [FAIL] word %u: %04x != %04x (linea %d)\n",
				    static_cast<unsigned>(i), storage[i], expect[i], __LINE__);
			++g_failures;
		}
	}
	for (eng::usize i = 16u; i < 24u; ++i) {
		CHECK(storage[i] == 0xbeefu); // runway intacto
	}
}

void test_wide() {
	std::printf("build: sprite de 32 px (width_words = 2) y HSTART completo\n");

	eng::u16 data[4] = {0x0123u, 0x4567u, 0x89abu, 0xcdefu};
	HwSpritePlacement plcs[1] = {plc(1u, 0x100u, 0x20u, 1u, 2u, false, chip(data))};

	eng::u16 storage[8];
	for (eng::u16& w : storage) {
		w = 0u;
	}
	SpriteDmaChain chain {};
	chain.storage = chip(storage);

	const eng::u16 expect[8] = {0x2080u, 0x2100u, 0x0123u, 0x4567u,
				    0x89abu, 0xcdefu, 0x0000u, 0x0000u};
	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 1u}, chain));
	CHECK(chain.used == 8u && chain.block_count == 1u);
	for (eng::usize i = 0u; i < 8u; ++i) {
		CHECK(storage[i] == expect[i]);
	}
}

void test_rejects() {
	std::printf("build: rechazos sin escritura (todo-o-nada)\n");

	eng::u16 data_a[6] = {};
	eng::u16 data_b[4] = {};
	eng::u16 data_short[5] = {};
	eng::u16 storage[32];
	SpriteDmaChain chain {};

	HwSpritePlacement ok_a = plc(3u, 0x40u, 0x30u, 3u, 1u, false, chip(data_a));
	HwSpritePlacement ok_b = plc(3u, 0x60u, 0x34u, 2u, 1u, false, chip(data_b));
	HwSpritePlacement plcs[2] = {ok_a, ok_b};

	auto fresh = [&]() {
		for (eng::u16& w : storage) {
			w = 0xdeadu;
		}
		chain = SpriteDmaChain {};
		chain.storage = chip(storage);
	};

	// Span vacío.
	fresh();
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {}, chain));
	CHECK(chain.used == 0u && chain.block_count == 0u && storage[0] == 0xdeadu);

	// Storage vacía.
	fresh();
	chain.storage = eng::ChipView<eng::SpriteTag> {};
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));

	// Bloque que abuta (vstart == bottom anterior): el DMA no tiene línea para la cabecera.
	fresh();
	plcs[1].vstart = 0x33u;
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));
	CHECK(chain.used == 0u && chain.block_count == 0u && storage[0] == 0xdeadu);

	// Solape.
	fresh();
	plcs[1].vstart = 0x32u;
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));

	// Canales mezclados.
	fresh();
	plcs[1].vstart = 0x34u;
	plcs[1].channel = 4u;
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));

	// ATTACH en canal par (no hay par 0+1 en este caso: el bit solo vale en el impar).
	fresh();
	plcs[1] = ok_b;
	plcs[0].channel = 2u;
	plcs[0].attach = true;
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));

	// Altura 0 / ancho 0 / DATA corta.
	fresh();
	plcs[0] = plc(3u, 0x40u, 0x30u, 0u, 1u, false, chip(data_a));
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 1u}, chain));
	plcs[0] = plc(3u, 0x40u, 0x30u, 3u, 0u, false, chip(data_a));
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 1u}, chain));
	plcs[0] = plc(3u, 0x40u, 0x30u, 3u, 1u, false, chip(data_short)); // 5 words < 6
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 1u}, chain));

	// Capacidad: 2 bloques + terminador = 16 words; con 14 no cabe.
	fresh();
	SpriteDmaChain small {};
	eng::u16 small_storage[14] {};
	small.storage = chip(small_storage);
	plcs[0] = ok_a;
	plcs[1] = ok_b;
	CHECK(!build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, small));
	CHECK(small.used == 0u && small.block_count == 0u);

	// Tope de bloques por canal (`kSpriteChainMaxBlocks`): un bloque de más no cabe.
	eng::u16 one[2] = {0x0001u, 0x0002u};
	HwSpritePlacement many[kSpriteChainMaxBlocks + 1u] {};
	for (eng::u8 i = 0u; i < kSpriteChainMaxBlocks + 1u; ++i) {
		many[i] = plc(3u, 0x10u, static_cast<eng::u16>(2u * i), 1u, 1u, false, chip(one));
	}
	eng::u16 big_storage[200] {}; // capacidad de sobra: el rechazo debe ser por el tope
	SpriteDmaChain big {};
	big.storage = chip(big_storage);
	CHECK(!build_sprite_dma_chain(
		eng::Span<const HwSpritePlacement> {many, kSpriteChainMaxBlocks + 1u}, big));
	CHECK(big.used == 0u && big.block_count == 0u);
}

void test_clamp() {
	std::printf("build: altura recortada a kSpriteMaxHeightLines (128)\n");

	eng::u16 src[260];
	for (eng::u16 i = 0u; i < 260u; ++i) {
		src[i] = static_cast<eng::u16>(0x1000u + i);
	}
	eng::u16 storage[264];
	for (eng::u16& w : storage) {
		w = 0xffffu;
	}

	HwSpritePlacement plcs[1] = {plc(3u, 0x40u, 0x10u, 130u, 1u, false, chip(src))};
	SpriteDmaChain chain {};
	chain.storage = chip(storage);

	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 1u}, chain));
	CHECK(chain.used == 260u); // 2 (cabecera) + 128*2 (DATA) + 2 (terminador)
	CHECK(storage[1] == 0x9000u); // VSTOP = 0x10 + 128 = 0x90 (recortado)
	CHECK(storage[2u + 255u] == src[255u]); // ultima word copiada
	CHECK(storage[2u + 256u] == 0u);        // terminador en su sitio
	CHECK(storage[261u] == 0xffffu && storage[263u] == 0xffffu); // runway intacto
}

void test_patch() {
	std::printf("patch: solo POS/CTL del bloque pedido (cadena persistente)\n");

	eng::u16 data_a[6] = {0x1111u, 0x2222u, 0x3333u, 0x4444u, 0x5555u, 0x6666u};
	eng::u16 data_b[4] = {0xaaaau, 0xbbbbu, 0xccccu, 0xddddu};
	eng::u16 storage[24] {};
	SpriteDmaChain chain {};

	HwSpritePlacement plcs[2] = {
		plc(3u, 0x40u, 0x30u, 3u, 1u, false, chip(data_a)),
		plc(3u, 0x60u, 0x34u, 2u, 1u, false, chip(data_b)),
	};
	chain.storage = chip(storage);
	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {plcs, 2u}, chain));

	// Parchea el bloque 1: nueva X (0x80), nueva linea (0x50) y ATTACH (canal 3 impar).
	CHECK(patch_sprite_chain_block(chain, 1u, 0x80u, 0x50u, 2u, true));
	CHECK(storage[8] == 0x5040u);
	CHECK(storage[9] == 0x5280u); // VSTOP=0x52 + ATTACH
	CHECK(storage[10] == 0xaaaau && storage[13] == 0xddddu); // DATA intacta
	CHECK(storage[0] == 0x3020u && storage[1] == 0x3300u);   // bloque 0 intacto

	// Rechazos: índice fuera, altura 0 y ATTACH en cadena de canal par.
	CHECK(!patch_sprite_chain_block(chain, 2u, 0u, 0u, 1u, false));
	CHECK(!patch_sprite_chain_block(chain, 0u, 0u, 0u, 0u, false));

	eng::u16 even_storage[10] {}; // 2 (cabecera) + 6 (DATA) + 2 (terminador)
	SpriteDmaChain even_chain {};
	HwSpritePlacement even_plc[1] = {plc(2u, 0x40u, 0x30u, 3u, 1u, false, chip(data_a))};
	even_chain.storage = chip(even_storage);
	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {even_plc, 1u}, even_chain));
	CHECK(!patch_sprite_chain_block(even_chain, 0u, 0x40u, 0x30u, 3u, true)); // ATTACH en par
	CHECK(patch_sprite_chain_block(even_chain, 0u, 0x40u, 0x30u, 3u, false));

	SpriteDmaChain empty {};
	CHECK(!patch_sprite_chain_block(empty, 0u, 0u, 0u, 1u, false));
}

// ---------------------------------------------------------------------------
// Emisión con el scheduler real ligado a un bloque Chip (patrón de HOST-428):
// armado por cadena y plan de estrategia. Los MOVEs se leen de la copperlist.
// ---------------------------------------------------------------------------

alignas(16) eng::u8 g_chip[8 * 1024];

eng::MemoryManager make_memory() {
	eng::MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	return mem;
}

eng::copper::Scheduler make_scheduler(eng::MemoryManager& mem, eng::u32 words) {
	return eng::copper::Scheduler {mem.chip().reserve<eng::CopperTag>(words * 2u, 16u)};
}

struct Mv {
	eng::u16 reg;
	eng::u16 val;
};

/// Un MOVE es `w0` par (registro) seguido de `w1` (dato). Devuelve cuántos aparecen.
unsigned collect_moves(const eng::u16* w, eng::u16 count, Mv* out, unsigned max) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count && n < max; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) continue; // WAIT (bit 0 = 1)
		out[n++] = Mv {w[i], w[i + 1u]};
	}
	return n;
}

const Mv* find(const Mv* mv, unsigned n, eng::u16 reg) {
	for (unsigned i = 0; i < n; ++i) {
		if (mv[i].reg == reg) return &mv[i];
	}
	return nullptr;
}

unsigned count_reg(const eng::u16* w, eng::u16 count, eng::u16 reg) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) == 0u && w[i] == reg) ++n;
	}
	return n;
}

unsigned count_wait(const eng::u16* w, eng::u16 count, eng::u16 line) {
	unsigned n = 0;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u && (w[i] >> 8u) == line) ++n;
	}
	return n;
}

void test_arm_dma_chain() {
	std::printf("arm_dma_chain: POS/CTL del primer bloque + PT a su DATA (el walk encadena)\n");

	alignas(4) eng::u16 chain_words[16] {};
	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 64u);

	SpriteManager::arm_dma_chain(sched, 3u, chip(chain_words), 40u, 20u, 8u, false);
	SpriteManager::arm_dma_chain(sched, 8u, chip(chain_words), 10u, 10u, 8u, false); // canal invalido
	SpriteManager::arm_dma_chain(sched, 4u, eng::ChipView<eng::SpriteTag> {}, 10u, 10u, 8u,
				     false); // vista vacia
	SpriteManager::arm_dma_chain(sched, 2u, chip(chain_words), 10u, 10u, 0u, false); // altura 0
	sched.end();

	Mv mv[16] {};
	const unsigned n = collect_moves(sched.data(), sched.words_used(), mv, 16u);
	CHECK(n == 4u); // POS/CTL/PTH/PTL del canal 3
	const Mv* p = find(mv, n, 0x158u); // SPR3POS = 0x140 + 3*8
	const Mv* c = find(mv, n, 0x15au);
	const Mv* h = find(mv, n, 0x12cu); // SPR3PTH = 0x120 + 3*4
	const Mv* l = find(mv, n, 0x12eu);
	CHECK(p != nullptr && c != nullptr && h != nullptr && l != nullptr);
	if (p && c) {
		CHECK(p->val == static_cast<eng::u16>((20u << 8u) | (40u >> 1u))); // VSTART=20, X=40
		CHECK(c->val == 0x1c00u); // VSTOP = 20 + 8 (exclusivo)
	}
	if (h && l) {
		// PT a la DATA del primer bloque: la cabecera POS/CTL (2 words) se salta.
		const eng::uintptr a = reinterpret_cast<eng::uintptr>(chain_words) + 4u;
		CHECK(h->val == static_cast<eng::u16>(a >> 16u));
		CHECK(l->val == static_cast<eng::u16>(a & 0xffffu));
	}
}

void test_placements_with_chain() {
	std::printf("emit_placements_into: canal con cadena (cero rearmes) + canal normal\n");

	alignas(4) eng::u16 d0[16] {};
	alignas(4) eng::u16 d1[16] {};
	alignas(4) eng::u16 d2[16] {};
	eng::MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);

	// Orden no decreciente de vstart: ch5@140, ch2@160, ch5@180 (multiplexado vertical).
	HwSpritePlacement ps[3] = {
		plc(5u, 40u, 140u, 8u, 1u, false, chip(d0)),
		plc(2u, 100u, 160u, 8u, 1u, false, chip(d1)),
		plc(5u, 40u, 180u, 8u, 1u, false, chip(d2)),
	};
	eng::u16 chain_storage[64] {}; // con runway de ceros
	SpriteDmaChain chains[8] {};
	chains[5].storage = chip(chain_storage);
	HwSpritePlacement ch5[2] = {ps[0], ps[2]};
	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {ch5, 2u}, chains[5]));

	SpriteManager::emit_placements_into(sched, eng::Span<const HwSpritePlacement> {ps, 3u}, 32u, {},
					    eng::Span<const SpriteDmaChain> {chains, 8u});
	sched.end();

	const eng::u16* w = sched.data();
	const eng::u16 count = sched.words_used();
	CHECK(count_wait(w, count, 32u) == 1u);  // un solo armado temprano
	CHECK(count_wait(w, count, 180u) == 0u); // el canal de la cadena NO rearma
	// El canal de la cadena se arma UNA vez: POS/CTL del primer bloque + PT a su DATA.
	CHECK(count_reg(w, count, 0x168u) == 1u); // SPR5POS (armado del bloque 1)
	CHECK(count_reg(w, count, 0x16au) == 1u); // SPR5CTL
	CHECK(count_reg(w, count, 0x134u) == 1u); // SPR5PTH (0x120+5*4)
	CHECK(count_reg(w, count, 0x136u) == 1u); // SPR5PTL
	CHECK(count_reg(w, count, 0x150u) == 1u); // SPR2POS (arm_object del canal suelto)
	CHECK(count_reg(w, count, 0x152u) == 1u); // SPR2CTL

	Mv mv[32] {};
	const unsigned n = collect_moves(w, count, mv, 32u);
	const Mv* pos5 = find(mv, n, 0x168u);
	const Mv* ptl5 = find(mv, n, 0x136u);
	const Mv* pos2 = find(mv, n, 0x150u);
	CHECK(pos5 != nullptr && ptl5 != nullptr && pos2 != nullptr);
	if (pos5) {
		CHECK(pos5->val == static_cast<eng::u16>((140u << 8u) | (40u >> 1u)));
	}
	if (ptl5) {
		// PT a la DATA del primer bloque (cabecera de 2 words por delante).
		const eng::uintptr a = reinterpret_cast<eng::uintptr>(chain_storage) + 4u;
		CHECK(ptl5->val == static_cast<eng::u16>(a & 0xffffu));
	}
	if (pos2) {
		CHECK(pos2->val == static_cast<eng::u16>((160u << 8u) | (100u >> 1u)));
	}

	// Degradación: un bloque MÁS del mismo canal que la cadena no cubre → rearme del canal.
	eng::MemoryManager mem2 = make_memory();
	eng::copper::Scheduler sched2 = make_scheduler(mem2, 256u);
	HwSpritePlacement ps2[4] = {ps[0], ps[1], ps[2],
				    plc(5u, 40u, 220u, 8u, 1u, false, chip(d0))};
	SpriteManager::emit_placements_into(sched2, eng::Span<const HwSpritePlacement> {ps2, 4u}, 32u,
					    {}, eng::Span<const SpriteDmaChain> {chains, 8u});
	sched2.end();
	const eng::u16* w2 = sched2.data();
	const eng::u16 count2 = sched2.words_used();
	CHECK(count_wait(w2, count2, 32u) == 1u);
	CHECK(count_wait(w2, count2, 180u) == 1u); // rearma (camino actual)
	CHECK(count_wait(w2, count2, 220u) == 1u);
	CHECK(count_reg(w2, count2, 0x168u) == 3u); // SPR5POS: armado + dos rearmes
}

void test_selector() {
	std::printf("choose_sprite_emit_plan: Rearm/Chain/ChainPersistent por canal\n");

	alignas(4) eng::u16 d0[16] {};
	alignas(4) eng::u16 d1[16] {};
	alignas(4) eng::u16 d2[16] {};
	HwSpritePlacement ps[3] = {
		plc(5u, 40u, 140u, 8u, 1u, false, chip(d0)),
		plc(2u, 100u, 160u, 8u, 1u, false, chip(d1)),
		plc(5u, 40u, 180u, 8u, 1u, false, chip(d2)),
	};
	eng::u16 chain_storage[64] {};
	SpriteDmaChain chains[8] {};
	chains[5].storage = chip(chain_storage);
	HwSpritePlacement ch5[2] = {ps[0], ps[2]};
	CHECK(build_sprite_dma_chain(eng::Span<const HwSpritePlacement> {ch5, 2u}, chains[5]));

	const eng::Span<const HwSpritePlacement> span_ps {ps, 3u};
	const eng::Span<const SpriteDmaChain> span_ch {chains, 8u};

	SpriteEmitPlan p0 = choose_sprite_emit_plan(span_ps, span_ch, 0u, SpriteChainBudget {0u});
	CHECK(p0.strategy[5] == SpriteEmitStrategy::Rearm); // sin presupuesto no hay cadena nueva
	CHECK(p0.strategy[2] == SpriteEmitStrategy::Rearm);

	SpriteEmitPlan p1 = choose_sprite_emit_plan(span_ps, span_ch, 0u, SpriteChainBudget {1000u});
	CHECK(p1.strategy[5] == SpriteEmitStrategy::Chain);
	CHECK(p1.strategy[2] == SpriteEmitStrategy::Rearm); // un solo bloque

	SpriteEmitPlan p2 = choose_sprite_emit_plan(span_ps, span_ch,
						    static_cast<eng::u8>(1u << 5u),
						    SpriteChainBudget {0u});
	CHECK(p2.strategy[5] == SpriteEmitStrategy::ChainPersistent); // no recopia DATA

	SpriteEmitPlan p3 = choose_sprite_emit_plan(span_ps, span_ch, 0u, SpriteChainBudget {20u});
	CHECK(p3.strategy[5] == SpriteEmitStrategy::Rearm); // DATA (36 words) > presupuesto

	chains[5].channel = 4u; // cadena con canal mal indexado: no lista
	SpriteEmitPlan p4 = choose_sprite_emit_plan(span_ps, span_ch, 0u, SpriteChainBudget {1000u});
	CHECK(p4.strategy[5] == SpriteEmitStrategy::Rearm);
}

} // namespace

int main() {
	std::printf("Test HOST-434 sprite_dma_chain (F3: cadena DMA por canal)\n");
	std::printf("=========================================================\n");

	test_encode();
	test_golden();
	test_wide();
	test_rejects();
	test_clamp();
	test_patch();
	test_arm_dma_chain();
	test_placements_with_chain();
	test_selector();

	if (g_failures == 0) {
		std::printf("OK: cadena DMA por canal validada (golden de words + armado + selector).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", g_failures);
	return 1;
}
