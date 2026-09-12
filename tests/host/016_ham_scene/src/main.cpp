// ============================================================================
// Test HOST-016: driver reutilizable `HamScene` (display planar con repeticion
// de filas / cuadruplicado, extraido del porte de fire-rgb).
// ============================================================================
//
// Valida en host, sin emulador, que el driver construye la geometria correcta:
//
//   1) `GraphicsDriver<HamScene, Backend>` (contrato takeover/install + id).
//   2) `emit_planes_display`: BPLCON0, DIW/DDF y punteros BPLxPT (reordenados).
//   3) Repeticion de filas: BPL1MOD/BPL2MOD = -bytes_per_row en todas las lineas
//      del grupo menos la ultima (que avanza con modulo 0); BPLCON1 alterno.
//   4) Paleta cargada y terminacion de lista.
//   5) Parametrico: un segundo config (sin repeticion, otros planos/BPLCON0)
//      produce otra geometria sin tocar el driver.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/016_ham_scene   (solo este)
//   bash tools/run-host-tests.sh                            (todos)

#include <cstdio>
#include <type_traits>

#include <eng/core/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/drivers/ham_scene.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u16;
using eng::u32;
using eng::graphics::drivers::HamScene;
using eng::graphics::drivers::HamSceneConfig;

/// Backend minimo que satisface los metodos que usan los drivers (templates).
struct MockBackend {
	const eng::u16* taken = nullptr;
	const eng::u16* installed = nullptr;
	void takeover_display(const eng::u16* words) { taken = words; }
	void install_copper_list(const eng::u16* words) { installed = words; }
};

// El driver cumple el contrato completo de driver grafico.
static_assert(eng::GraphicsDriver<HamScene, MockBackend>);
static_assert(eng::DisplayDriver<HamScene, MockBackend>);

alignas(16) eng::u8 g_chip[256 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena { g_chip, sizeof(g_chip), MemoryKind::Chip };
	return mem;
}

/// Recorre la copperlist en pares (word0, word1) contando MOVEs de un registro.
struct MoveTally {
	unsigned mod_back = 0;    // BPL1MOD = -bytes_per_row
	unsigned mod_zero = 0;    // BPL1MOD = 0
	unsigned bplcon1_shift = 0;
	unsigned bplcon1_zero = 0;
	unsigned waits = 0;
	unsigned color_moves = 0;
	unsigned bplcon0_moves = 0;
};

MoveTally tally(const eng::u16* words, eng::u16 count, eng::u16 row_back) {
	MoveTally t {};
	bool after_wait = false;
	for (eng::u16 i = 0; i + 1u < count; i += 2u) {
		const eng::u16 a = words[i];
		if (a == 0xffffu) break;                     // fin de lista
		if ((a & 1u) != 0u) { ++t.waits; after_wait = true; continue; } // WAIT (bit0 = 1)
		const eng::u16 value = words[i + 1u];
		if (a == static_cast<eng::u16>(eng::copper::Register::BPLCON0)) {
			++t.bplcon0_moves;
		} else if (a >= 0x180u && a <= 0x1beu) {
			++t.color_moves;
		} else if (after_wait &&
			   a == static_cast<eng::u16>(eng::copper::Register::BPL1MOD)) {
			if (value == row_back) ++t.mod_back;
			else if (value == 0u) ++t.mod_zero;
		} else if (after_wait &&
			   a == static_cast<eng::u16>(eng::copper::Register::BPLCON1)) {
			if (value == 0x0022u) ++t.bplcon1_shift;
			else if (value == 0u) ++t.bplcon1_zero;
		}
	}
	return t;
}

} // namespace

int main() {
	// --- 1) HAM + cuadruplicado (config de la demo 080) -----------------------
	{
		MemorySystem mem = make_memory();
		HamSceneConfig cfg {};
		cfg.rows = 64;
		cfg.planes = 4;
		cfg.bytes_per_row = 40;
		cfg.bplcon0 = 0x7a00u;
		cfg.first_line = 0x2cu;
		cfg.row_repeat = 4;
		cfg.bplcon1_shift = 0x0022u;
		cfg.reverse_plane_ptrs = true;
		static const eng::u16 palette[16] {};
		cfg.palette = palette;
		cfg.palette_count = 16;

		HamScene scene;
		if (!scene.init(mem, cfg)) {
			std::printf("[FAIL] HamScene::init fallo\n");
			return 1;
		}
		if (scene.plane_bytes() != 40u * 64u || scene.plane_count() != 4u) {
			std::printf("[FAIL] geometria de planos incorrecta (bytes=%u planos=%u)\n",
				    (unsigned)scene.plane_bytes(), (unsigned)scene.plane_count());
			return 1;
		}

		// takeover/install delegan en el backend con la misma copperlist.
		MockBackend backend;
		scene.takeover(backend);
		scene.install(backend);
		if (backend.taken != scene.copper_words_ptr() || backend.installed != scene.copper_words_ptr()) {
			std::printf("[FAIL] takeover/install no pasan la copperlist del driver\n");
			return 1;
		}

		const u16 row_back = static_cast<u16>(0u - 40u); // 0xffd8
		const MoveTally t = tally(scene.copper_words_ptr(), scene.copper_words(), row_back);
		// 64 grupos x 4 lineas = 256 lineas; 3 repeticiones + 1 avance por grupo.
		if (t.mod_back != 64u * 3u || t.mod_zero != 64u) {
			std::printf("[FAIL] cuadruplicado: mod_back=%u (esperado 192) mod_zero=%u (64)\n",
				    t.mod_back, t.mod_zero);
			return 1;
		}
		if (t.waits != 257u) {
			// 256 lineas + 1 WAIT de overflow PAL (al pasar de la linea 255).
			std::printf("[FAIL] nº de WAITs = %u (esperado 257)\n", (unsigned)t.waits);
			return 1;
		}

		if (t.bplcon1_shift != 128u || t.bplcon1_zero != 128u) {
			std::printf("[FAIL] BPLCON1 alterno: shift=%u (128) zero=%u (128)\n",
				    t.bplcon1_shift, t.bplcon1_zero);
			return 1;
		}
		if (t.bplcon0_moves != 1u) {
			std::printf("[FAIL] BPLCON0 no emitido una vez (=%u)\n", t.bplcon0_moves);
			return 1;
		}
		if (t.color_moves != 16u) {
			std::printf("[FAIL] paleta incompleta (color_moves=%u, esperado 16)\n", t.color_moves);
			return 1;
		}
		if (!scene.copper_report().ok) {
			std::printf("[FAIL] copper report no ok\n");
			return 1;
		}
	}

	// --- 2) Parametrico: sin repeticion, 5 planos, otro BPLCON0 ---------------
	{
		MemorySystem mem = make_memory();
		HamSceneConfig cfg {};
		cfg.rows = 128;
		cfg.planes = 5;
		cfg.bytes_per_row = 40;
		cfg.bplcon0 = 0x5000u;   // 5 planos, sin modos especiales
		cfg.row_repeat = 1;
		cfg.bplcon1_shift = 0u;
		cfg.palette = nullptr;
		cfg.palette_count = 0;

		HamScene scene;
		if (!scene.init(mem, cfg) || !scene.ok()) {
			std::printf("[FAIL] HamScene::init (config plano) fallo\n");
			return 1;
		}
		const u16 row_back = static_cast<u16>(0u - 40u);
		const MoveTally t = tally(scene.copper_words_ptr(), scene.copper_words(), row_back);
		// row_repeat = 1: cada linea es "ultima del grupo" -> modulo 0, sin repetir.
		if (t.mod_back != 0u || t.mod_zero != 128u) {
			std::printf("[FAIL] sin repeticion: mod_back=%u (0) mod_zero=%u (128)\n",
				    t.mod_back, t.mod_zero);
			return 1;
		}
		if (t.waits != 128u || t.bplcon1_shift != 0u) {
			std::printf("[FAIL] sin repeticion: waits=%u (128) bplcon1_shift=%u (0)\n",
				    t.waits, t.bplcon1_shift);
			return 1;
		}
	}

	std::printf("OK: HamScene valida display planar + repeticion de filas (cuadruplicado) y es parametrico.\n");
	return 0;
}
