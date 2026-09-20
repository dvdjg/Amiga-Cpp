// ============================================================================
// Test HOST-067: doble/triple buffer de display en `scene::compose` (parcheo
// de `BPLxPT` en `commit`).
// ============================================================================
//
// Sustituye a `MultiBuffered<Driver, N>`: el modelo de escena ya cubre el patron
// "N buffers de display + swap" parcheando los `BPLxPT` de la copperlist en
// `Scene::commit()`. Valida en host (g++ nativo, sin WinAmiga):
//
//   1) `buffers = N` reserva N bitmaps distintos y arranca dibujando en el trasero.
//   2) `commit` repunta los `BPLxPT` al buffer publicado y rota (0->1->0 con N=2).
//   3) N=1: un solo buffer, `commit` no cambia de buffer.
//   4) `reverse_ptrs` registra los parches incluso en orden inverso (case fire-rgb).
//   5) Los `BPLxPT` de dos buffers distintos difieren (cada lista apunta a su bitmap).
//
// El backend es de pega (solo registra que copperlist se toma/instala): nada de
// hardware ni RAM Amiga.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/067_multi_buffered

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/scene/compose.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u8;
using eng::u16;
using eng::u32;
using eng::graphics::scene::Scene;
using eng::graphics::scene::SceneResources;

alignas(16) eng::u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

/// Extrae los punteros BPLxPT (par hi/lo por plano). Como `reverse_ptrs` reemite los
/// punteros (el último manda), se toma el **último** `BPLxPTH` de cada plano.
struct PlanePtrs {
	u32 addr[4] = {0u, 0u, 0u, 0u};
};

PlanePtrs bplxpt(const u16* words, u16 count) {
	PlanePtrs p {};
	for (u16 i = 0; i + 1u < count; i += 2u) {
		for (u8 plane = 0; plane < 4u; ++plane) {
			if (words[i] == static_cast<u16>(eng::copper::bitplane_pointer_high_register(plane))) {
				p.addr[plane] = (static_cast<u32>(words[i + 1u]) << 16) |
						(words[i + 3u] & 0xffffu);
			}
		}
	}
	return p;
}

} // namespace

int main() {
	// --- 1..2) N=2: dos bitmaps y commit rota y repunta -----------------------
	{
		MemorySystem mem = make_memory();
		SceneResources res = eng::graphics::scene::planar4(320, 256, 4);
		res.buffers = 2;
		Scene sc;
		if (!eng::graphics::scene::compose(
			    sc, mem, res,
			    eng::graphics::scene::display(eng::graphics::scene::kPal320x256,
							  eng::graphics::scene::kBplcon0_4Planes))) {
			std::printf("[FAIL] compose N=2 fallo\n");
			return 1;
		}
		if (sc.buffer_count() != 2u) {
			std::printf("[FAIL] buffer_count=%u (esperado 2)\n", (unsigned)sc.buffer_count());
			return 1;
		}
		if (sc.buffer(0).data() == sc.buffer(1).data()) {
			std::printf("[FAIL] los buffers comparten el bloque de planos\n");
			return 1;
		}
		if (sc.back_index() != 1u) {
			std::printf("[FAIL] back_index inicial=%u (esperado 1: no dibujar lo visible)\n",
				    (unsigned)sc.back_index());
			return 1;
		}

		// `display` emite los punteros del buffer trasero inicial (1); `commit` publica el
		// buffer dibujado y rota: el primer commit confirma el 1, el segundo repunta al 0.
		const u16* w = sc.plan().active_words();
		const PlanePtrs p1 = bplxpt(w, sc.words());
		sc.commit(); // publica el buffer 1 y rota a 0
		if (sc.back_index() != 0u) {
			std::printf("[FAIL] commit no rota 1->0\n");
			return 1;
		}
		sc.commit(); // publica el buffer 0: aquí sí se repuntan los BPLxPT
		const PlanePtrs p0 = bplxpt(w, sc.words());
		if (p1.addr[0] == p0.addr[0] || p1.addr[0] == 0u) {
			std::printf("[FAIL] los BPLxPT no se repuntan en commit\n");
			return 1;
		}
		if (sc.back_index() != 1u) {
			std::printf("[FAIL] commit no rota 0->1\n");
			return 1;
		}
	}

	// --- 3) N=1: un solo buffer, commit no cambia de buffer -------------------
	{
		MemorySystem mem = make_memory();
		Scene sc;
		if (!eng::graphics::scene::compose(
			    sc, mem, eng::graphics::scene::planar4(320, 256, 4),
			    eng::graphics::scene::display(eng::graphics::scene::kPal320x256,
							  eng::graphics::scene::kBplcon0_4Planes))) {
			std::printf("[FAIL] compose N=1 fallo\n");
			return 1;
		}
		if (sc.buffer_count() != 1u || sc.back_index() != 0u) {
			std::printf("[FAIL] N=1: count=%u back=%u (esperado 1/0)\n",
				    (unsigned)sc.buffer_count(), (unsigned)sc.back_index());
			return 1;
		}
		const u16* w = sc.plan().active_words();
		const u32 a0 = bplxpt(w, sc.words()).addr[0];
		sc.commit();
		const u32 a1 = bplxpt(w, sc.words()).addr[0];
		if (sc.back_index() != 0u || a0 != a1) {
			std::printf("[FAIL] N=1: commit debe quedarse en el buffer 0\n");
			return 1;
		}
	}

	// --- 4) reverse_ptrs: los parches de plano siguen registrandose -----------
	{
		MemorySystem mem = make_memory();
		SceneResources res = eng::graphics::scene::ham(320, 256, 64, 4);
		res.buffers = 2;
		res.copper_bytes = 8192u;
		Scene sc;
		if (!eng::graphics::scene::compose(
			    sc, mem, res,
			    eng::graphics::scene::display(eng::graphics::scene::kPal320x256,
							  eng::graphics::scene::kBplcon0_Ham6),
			    eng::graphics::scene::reverse_ptrs(),
			    eng::graphics::scene::row_repeat(4u, 0x2cu, 0x0022u))) {
			std::printf("[FAIL] compose reverse_ptrs fallo\n");
			return 1;
		}
		const u16* w = sc.plan().active_words();
		const u32 b1 = bplxpt(w, sc.words()).addr[0];
		sc.commit();
		sc.commit();
		const u32 b0 = bplxpt(w, sc.words()).addr[0];
		if (b1 == b0 || b1 == 0u) {
			std::printf("[FAIL] reverse_ptrs: commit no repunta el BPLxPT efectivo\n");
			return 1;
		}
	}

	std::printf("OK: scene::compose doble buffer (parcheo de BPLxPT en commit).\n");
	return 0;
}
