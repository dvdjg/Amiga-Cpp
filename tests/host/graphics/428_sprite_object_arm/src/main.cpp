// ============================================================================
// Test HOST-428: armado de un objeto de sprite (`SpriteManager::arm_object`).
// ============================================================================
//
// Valida en host el helper de armado de objetos de sprite del engine
// (`graphics/sprite_manager.hpp`): escribe `SPRxPTH/L` a la DATA, `SPRxPOS` y `SPRxCTL`
// (VSTOP exclusivo, AHRM cap. 4; `ATTACH` en el canal impar). Es el patrón validado por la
// demo `214_attached_object` (par *attached* de 15 colores + chispas) y por el armado de
// objetos de la 208.
//
// Comprobaciones:
//   1) Paquete par+impar *attached*: POS iguales, VSTOP = y+alto, ATTACH solo en el impar,
//      `SPRxPT` a la DATA de cada canal.
//   2) Paridad de la X: bit 0 de `SPRxCTL` y HSTART/2 en `SPRxPOS`.
//   3) Rechazos sin emisión: canal >= 8, DATA vacía, alto 0.
//   4) `emit_armed_into`: un solo WAIT temprano para todos los canales.
//   5) `emit_placements_into`: primera config por canal en el armado y REARME vertical del
//      canal reutilizado en otra franja (multiplexado del allocator; demo 216).
//
// El scheduler se sustituye por el real ligado a un bloque Chip del arena (como HOST-105);
// los MOVEs se leen de la copperlist construida.
//
// Ejecución:
//   bash tools/run-host-tests.sh tests/host/graphics/428_sprite_object_arm   (solo este)
//   bash tools/run-host-tests.sh                                            (todos)

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/memory/memory_manager.hpp>

namespace {

using eng::MemoryManager;
using eng::u16;
using eng::u32;

alignas(16) eng::u8 g_chip[8 * 1024];

MemoryManager make_memory() {
	MemoryManager mem;
	mem.configure(g_chip, sizeof(g_chip), nullptr, 0u, nullptr, 0u, 16u);
	return mem;
}

eng::copper::Scheduler make_scheduler(MemoryManager& mem, u32 words) {
	return eng::copper::Scheduler {mem.chip().reserve<eng::CopperTag>(words * 2u, 16u)};
}

/// Vista Chip sobre un array local (en host el banco es solo tipo).
template <eng::usize N>
eng::ChipView<eng::SpriteTag> chip(eng::u16 (&arr)[N]) {
	return eng::ChipView<eng::SpriteTag> {
		eng::Address<eng::MemoryKind::Chip>::from_storage(arr), N * 2u};
}

unsigned g_fail = 0;
#define CHECK(cond, msg)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			std::printf("FAIL: %s (linea %d)\n", msg, __LINE__);   \
			++g_fail;                                              \
		}                                                              \
	} while (0)

struct Mv {
	u16 reg;
	u16 val;
};

/// Un MOVE es `w0` par (registro) seguido de `w1` (dato). Devuelve cuántos aparecen.
unsigned collect_moves(const u16* w, u16 count, Mv* out, unsigned max) {
	unsigned n = 0;
	for (u16 i = 0; i + 1u < count && n < max; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) != 0u) continue; // WAIT (bit 0 = 1)
		out[n++] = Mv {w[i], w[i + 1u]};
	}
	return n;
}

const Mv* find(const Mv* mv, unsigned n, u16 reg) {
	for (unsigned i = 0; i < n; ++i) {
		if (mv[i].reg == reg) return &mv[i];
	}
	return nullptr;
}

void test_attached_pair() {
	std::printf("arm_object: par attached con POS/CTL/PT correctos\n");

	alignas(4) u16 even[64] {};
	alignas(4) u16 odd[64] {};
	MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);

	const u16 x = 160u;   // par -> HSTART=160, HSTART[0]=0
	const u16 y = 100u;
	const u16 h = 24u;
	eng::graphics::SpriteManager::arm_object(sched, 0u, chip(even), x, y, h, false);
	eng::graphics::SpriteManager::arm_object(sched, 1u, chip(odd), x, y, h, true);
	sched.end();

	Mv mv[32] {};
	const unsigned n = collect_moves(sched.data(), sched.words_used(), mv, 32u);

	const Mv* p0 = find(mv, n, 0x140u);
	const Mv* c0 = find(mv, n, 0x142u);
	const Mv* h0 = find(mv, n, 0x120u);
	const Mv* l0 = find(mv, n, 0x122u);
	const Mv* p1 = find(mv, n, 0x148u);
	const Mv* c1 = find(mv, n, 0x14Au);
	const Mv* l1 = find(mv, n, 0x126u);
	CHECK(p0 && c0 && h0 && l0 && p1 && c1 && l1, "se emiten POS/CTL/PT de los dos canales");

	if (p0 && c0 && p1 && c1 && h0 && l0 && l1) {
		const u16 pos = static_cast<u16>((y << 8u) | ((x >> 1u) & 0xffu));
		const u16 ctl = static_cast<u16>((y + h) << 8u); // VSTOP exclusivo
		CHECK(p0->val == pos, "SPR0POS = VSTART/HSTART>>1");
		CHECK(c0->val == ctl, "SPR0CTL = VSTOP sin ATTACH");
		CHECK(p1->val == pos, "SPR1POS identico (misma posicion)");
		CHECK(c1->val == static_cast<u16>(ctl | 0x0080u), "SPR1CTL con ATTACH (bit 7)");
		// PT a la DATA de cada canal (pointer chunks de 16 bits).
		const eng::uintptr a0 = reinterpret_cast<eng::uintptr>(even);
		const eng::uintptr a1 = reinterpret_cast<eng::uintptr>(odd);
		const u16 hi0 = static_cast<u16>(a0 >> 16u);
		const u16 lo0 = static_cast<u16>(a0 & 0xffffu);
		const u16 lo1 = static_cast<u16>(a1 & 0xffffu);
		CHECK(h0->val == hi0 && l0->val == lo0, "SPR0PTH/L apunta a la DATA");
		CHECK(l1->val == lo1, "SPR1PTL apunta a la DATA del impar");
	}
	// Los canales 2..7 no se tocan.
	for (eng::u8 c = 2u; c < 8u; ++c) {
		CHECK(find(mv, n, static_cast<u16>(0x140u + c * 8u)) == nullptr,
		      "canal no armado no se escribe");
	}
}

void test_odd_x_parity() {
	std::printf("arm_object: X impar -> bit 0 de SPRxCTL\n");

	alignas(4) u16 data[64] {};
	MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);
	eng::graphics::SpriteManager::arm_object(sched, 2u, chip(data), 143u, 50u, 16u, false);
	sched.end();

	Mv mv[16] {};
	const unsigned n = collect_moves(sched.data(), sched.words_used(), mv, 16u);
	const Mv* p = find(mv, n, 0x150u);
	const Mv* c = find(mv, n, 0x152u);
	CHECK(p && c, "POS/CTL emitidos para el canal 2");
	if (p && c) {
		CHECK(p->val == static_cast<u16>((50u << 8u) | (71u & 0xffu)), "HSTART>>1 = 71");
		CHECK((c->val & 0x1u) != 0u, "bit 0 de CTL = HSTART[0] (X impar)");
		CHECK((c->val >> 8u) == 66u, "VSTOP = y+alto = 66");
	}
}

/// `emit_armed_into`: un solo `WAIT` en la línea de armado y todos los canales habilitados
/// (ningún `WAIT` por `VSTART`).
void test_manager_armed_into() {
	std::printf("emit_armed_into: un WAIT temprano y los canales habilitados\n");

	alignas(4) u16 d0[64] {};
	alignas(4) u16 d1[64] {};
	MemoryManager mem = make_memory();
	eng::graphics::SpriteManager sm {};
	CHECK(sm.init(mem, 256u), "SpriteManager init");

	eng::graphics::SpriteConfig c0 {};
	c0.enabled = true;
	c0.data = chip(d0);
	c0.width_words = 1u;
	c0.height = 16u;
	c0.hpos = 100u;
	c0.vstart = 120u;
	c0.vstop = 136u;
	eng::graphics::SpriteConfig c1 = c0;
	c1.data = chip(d1);
	c1.attach = true;
	sm.set(0u, c0);
	sm.set(1u, c1);

	eng::copper::Scheduler sched = make_scheduler(mem, 256u);
	sm.emit_armed_into(sched, 32u);
	sched.end();

	const u16* w = sched.data();
	const u16 count = sched.words_used();
	unsigned waits32 = 0, waits120 = 0;
	for (u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) == 0u) continue;
		if ((w[i] >> 8u) == 32u) ++waits32;
		if ((w[i] >> 8u) == 120u) ++waits120;
	}
	CHECK(waits32 == 1u, "un solo WAIT en la linea de armado (32)");
	CHECK(waits120 == 0u, "ningun WAIT por VSTART (patron de objetos)");

	Mv mv[32] {};
	const unsigned n = collect_moves(w, count, mv, 32u);
	const Mv* p0 = find(mv, n, 0x140u);
	const Mv* c1v = find(mv, n, 0x14Au);
	CHECK(p0 != nullptr, "SPR0POS emitido");
	CHECK(c1v != nullptr && (c1v->val & 0x0080u) != 0u, "SPR1CTL con ATTACH");
	if (p0) {
		CHECK(p0->val == static_cast<u16>((120u << 8u) | 50u), "POS del canal 0");
	}
}

/// `emit_placements_into`: primera config de cada canal en la línea de armado y **rearme
/// vertical** de un canal reutilizado por el allocator en otra franja (multiplexado).
void test_placements_multiplex() {
	std::printf("emit_placements_into: rearme vertical del mismo canal\n");

	alignas(4) u16 d0[64] {};
	alignas(4) u16 d1[64] {};
	MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 256u);

	// Lista en orden no decreciente de `vstart`: par 0/1 en [100,116), ch5 en [120,136),
	// y el canal 0 REUTILIZADO en [140,156) (franja disjunta: multiplexado vertical).
	eng::graphics::HwSpritePlacement ps[4] {};
	ps[0].channel = 0u;
	ps[0].hpos = 80u;
	ps[0].vstart = 100u;
	ps[0].height = 16u;
	ps[0].data = chip(d0);
	ps[1].channel = 1u;
	ps[1].hpos = 80u;
	ps[1].vstart = 100u;
	ps[1].height = 16u;
	ps[1].attach = true;
	ps[1].data = chip(d1);
	ps[2].channel = 5u;
	ps[2].hpos = 16u;
	ps[2].vstart = 120u;
	ps[2].height = 16u;
	ps[2].data = chip(d0);
	ps[3].channel = 0u;
	ps[3].hpos = 200u;
	ps[3].vstart = 140u;
	ps[3].height = 16u;
	ps[3].data = chip(d1);

	eng::graphics::SpriteManager::emit_placements_into(
		sched, eng::Span<const eng::graphics::HwSpritePlacement> {ps, 4u}, 32u);
	sched.end();

	const u16* w = sched.data();
	const u16 count = sched.words_used();
	unsigned waits32 = 0, waits140 = 0;
	for (u16 i = 0; i + 1u < count; i += 2u) {
		if (w[i] == 0xffffu) break;
		if ((w[i] & 1u) == 0u) continue;
		if ((w[i] >> 8u) == 32u) ++waits32;
		if ((w[i] >> 8u) == 140u) ++waits140;
	}
	CHECK(waits32 == 1u, "un solo WAIT compartido en la linea de armado (32)");
	CHECK(waits140 == 1u, "rearme del canal 0 en su nueva franja (140)");

	// Cuenta de MOVEs por registro y ultimo valor (el rearme pisa al primero).
	auto count_reg = [&](u16 reg, u16* last) {
		unsigned n = 0;
		for (u16 i = 0; i + 1u < count; i += 2u) {
			if (w[i] == 0xffffu) break;
			if ((w[i] & 1u) != 0u) continue;
			if (w[i] == reg) {
				++n;
				if (last != nullptr) *last = w[i + 1u];
			}
		}
		return n;
	};
	u16 pos0 = 0u, ctl1 = 0u, pos5 = 0u;
	CHECK(count_reg(0x140u, &pos0) == 2u, "SPR0POS: armado + rearme (dos escrituras)");
	CHECK(pos0 == static_cast<u16>((140u << 8u) | (200u >> 1u)), "ultimo POS0 en la franja 140");
	CHECK(count_reg(0x148u, nullptr) == 1u, "SPR1POS: una sola (primera config en el armado)");
	CHECK(count_reg(0x14Au, &ctl1) == 1u && (ctl1 & 0x0080u) != 0u, "SPR1CTL con ATTACH");
	CHECK(count_reg(0x168u, &pos5) == 1u, "SPR5POS una sola (canal no reutilizado)");
	CHECK(pos5 == static_cast<u16>((120u << 8u) | (16u >> 1u)), "POS5 en su franja");

	// Extra de paleta entre el armado y el rearme: se intercala por línea (nunca espera
	// hacia atrás) y el MOVE de COLOR17 va antes del rearme de la franja 140.
	eng::copper::Scheduler sched2 = make_scheduler(mem, 256u);
	u16 color = 0x0f00u;
	eng::graphics::SpritePaletteEvent ev {130u, &color, 17u, 1u};
	eng::graphics::SpriteManager::emit_placements_into(
		sched2, eng::Span<const eng::graphics::HwSpritePlacement> {ps, 4u}, 32u,
		eng::Span<const eng::graphics::SpritePaletteEvent> {&ev, 1u});
	sched2.end();
	const u16* w2 = sched2.data();
	const u16 count2 = sched2.words_used();
	unsigned waits130 = 0;
	u16 color_at = 0xffffu, pos0_at = 0xffffu, pos0_seen = 0u;
	for (u16 i = 0; i + 1u < count2; i += 2u) {
		if (w2[i] == 0xffffu) break;
		if ((w2[i] & 1u) != 0u) {
			if ((w2[i] >> 8u) == 130u) ++waits130;
			continue;
		}
		if (w2[i] == 0x1a2u && color_at == 0xffffu) color_at = i;
		if (w2[i] == 0x140u) {
			++pos0_seen;
			if (pos0_seen == 2u) pos0_at = i;
		}
	}
	CHECK(waits130 == 1u, "WAIT de la paleta en la linea 130");
	CHECK(color_at != 0xffffu && w2[color_at + 1u] == 0x0f00u, "COLOR17 del extra");
	CHECK(color_at < pos0_at, "paleta antes del rearme de la franja 140");
}

void test_rejects() {
	std::printf("arm_object: rechazos sin emision\n");

	alignas(4) u16 data[64] {};
	MemoryManager mem = make_memory();
	eng::copper::Scheduler sched = make_scheduler(mem, 64u);
	eng::graphics::SpriteManager::arm_object(sched, 8u, chip(data), 10u, 10u, 16u, false);
	eng::graphics::SpriteManager::arm_object(sched, 0u,
						 eng::ChipView<eng::SpriteTag> {}, 10u, 10u, 16u,
						 false);
	eng::graphics::SpriteManager::arm_object(sched, 0u, chip(data), 10u, 10u, 0u, false);
	sched.end();

	Mv mv[16] {};
	const unsigned n = collect_moves(sched.data(), sched.words_used(), mv, 16u);
	CHECK(n == 0u, "canal invalido/DATA vacia/alto 0 no emiten nada");
}

} // namespace

int main() {
	std::printf("Test HOST-428 sprite_object_arm (armado de objeto de sprite)\n");
	std::printf("================================================================\n");

	test_attached_pair();
	test_odd_x_parity();
	test_manager_armed_into();
	test_placements_multiplex();
	test_rejects();

	if (g_fail == 0) {
		std::printf("OK: armado de objeto de sprite validado.\n");
		return 0;
	}
	std::printf("FAIL: %u comprobacion(es) fallaron\n", g_fail);
	return 1;
}
