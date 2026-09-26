// ============================================================================
// Test HOST-067: doble/triple buffer de display en `scene::compose`.
// ============================================================================
//
// Sustituye a `MultiBuffered<Driver, N>`: el modelo de escena ya cubre el patron
// "N buffers de display + swap", y lo **observa** con `Scene::display_plane_uses`
// (nada de recorrer la copperlist con punteros raw). Valida en host (g++ nativo):
//
//   1) `buffers = N` reserva N bitmaps distintos y arranca dibujando en el trasero.
//   2) `commit` repunta los `BPLxPT` al buffer publicado y rota (0->1->0 con N=2).
//   3) N=1: un solo buffer, `commit` no cambia de buffer.
//   4) `reverse_ptrs` registra los parches incluso en orden inverso (case fire-rgb).
//
// El backend es de pega (solo registra que copperlist se toma/instala): nada de
// hardware ni RAM Amiga.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/067_multi_buffered

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/composition/compose.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u8;
using eng::graphics::composition::Scene;
using eng::graphics::composition::SceneResources;

alignas(16) eng::u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = eng::ChipArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

int failures = 0;
void check(bool ok, const char* msg) {
	if (!ok) {
		std::printf("  [FAIL] %s\n", msg);
		++failures;
	}
}

} // namespace

int main() {
	// --- 1..2) N=2: dos bitmaps, commit rota y repunta ------------------------
	{
		MemorySystem mem = make_memory();
		SceneResources res = eng::graphics::composition::planar(320, 256, 4);
		res.buffers = 2;
		Scene sc;
		check(eng::graphics::composition::compose(
			      sc, mem, res, eng::graphics::composition::ocs_a500,
			      eng::graphics::composition::display(eng::graphics::composition::kPal320x256,
							    eng::graphics::composition::kBplcon0_4Planes)),
		      "compose N=2");
		check(sc.buffer_count() == 2u, "buffer_count == 2");
		check(sc.buffer(0).data() != sc.buffer(1).data(), "los buffers son distintos");
		check(sc.back_index() == 1u, "arranca en el trasero (1)");
		// El BPLxPT del plano 0 apunta al buffer trasero inicial (1).
		check(sc.display_plane_uses(0u, 1u), "el plano 0 usa el buffer 1 al inicio");

		// `commit` publica el buffer dibujado y rota.
		sc.commit();
		check(sc.back_index() == 0u, "commit rota 1->0");
		check(sc.display_plane_uses(0u, 1u), "tras commit el display sigue en el buffer 1");
		sc.commit();
		check(sc.back_index() == 1u, "commit rota 0->1");
		check(sc.display_plane_uses(0u, 0u), "tras el 2º commit el display usa el buffer 0");
	}

	// --- 3) N=1: un solo buffer, commit no cambia ----------------------------
	{
		MemorySystem mem = make_memory();
		Scene sc;
		check(eng::graphics::composition::compose(
			      sc, mem, eng::graphics::composition::planar(320, 256, 4),
			      eng::graphics::composition::ocs_a500,
			      eng::graphics::composition::display(eng::graphics::composition::kPal320x256,
							    eng::graphics::composition::kBplcon0_4Planes)),
		      "compose N=1");
		check(sc.buffer_count() == 1u && sc.back_index() == 0u, "N=1: count/back 1/0");
		check(sc.display_plane_uses(0u, 0u), "el plano 0 usa el buffer 0");
		sc.commit();
		check(sc.back_index() == 0u && sc.display_plane_uses(0u, 0u),
		      "N=1: commit se queda en el buffer 0");
	}

	// --- 4) reverse_ptrs: los parches de plano siguen registrandose ----------
	{
		MemorySystem mem = make_memory();
		SceneResources res = eng::graphics::composition::planar(320, 256, 4);
		res.rows = 64;
		res.buffers = 2;
		res.copper_bytes = 8192u;
		Scene sc;
		check(eng::graphics::composition::compose(
			      sc, mem, res, eng::graphics::composition::ocs_a500,
			      eng::graphics::composition::display(eng::graphics::composition::kPal320x256,
							    eng::graphics::composition::kBplcon0_Ham6),
			      eng::graphics::composition::reverse_ptrs(),
			      eng::graphics::composition::row_repeat(4u, 0x2cu, 0x0022u)),
		      "compose reverse_ptrs");
		check(sc.display_plane_uses(0u, 1u), "reverse_ptrs: el plano 0 usa el buffer 1");
		sc.commit();
		sc.commit();
		check(sc.display_plane_uses(0u, 0u), "reverse_ptrs: el display usa el buffer 0");
	}

	if (failures == 0) {
		std::printf("OK: scene::compose doble buffer (parcheo de BPLxPT en commit).\n");
		return 0;
	}
	std::printf("FAIL: %d comprobacion(es) fallaron\n", failures);
	return 1;
}
