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
	test_rejects();

	if (g_fail == 0) {
		std::printf("OK: armado de objeto de sprite validado.\n");
		return 0;
	}
	std::printf("FAIL: %u comprobacion(es) fallaron\n", g_fail);
	return 1;
}
