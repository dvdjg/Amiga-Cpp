// ============================================================================
// Test HOST-069: `copper::DoubleBuffer` y su uso por `TileScrollScene`
// ============================================================================
//
// El doble buffer de copperlist estaba reimplementado en cuatro sitios. Ahora vive en
// `eng/graphics/copper/double_buffer.hpp`. Este test lo valida en dos partes:
//
//   A) API del `DoubleBuffer`: reserva dos bloques distintos, `active` arranca en 1
//      (el inactivo, 0, es el primero en escribirse), `flip()` alterna, y
//      `takeover`/`install` publican el bloque activo.
//
//   B) Integracion con `TileScrollScene` (EHB): el primer `rebuild` emite la lista
//      completa en AMBOS bloques; los siguientes **parchean el inactivo** (BPLCON1 y
//      los punteros) y publican. Invariante que se comprueba: tras un scroll, el
//      bloque activo tiene el valor nuevo y el que deja de estar activo conserva el
//      viejo (no se ha tocado).
//
// Los valores se localizan **escaneando la lista por registro** (no por offsets
// cableados), que es justo lo que el refactor elimina.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/graphics/069_copper_double_buffer

#include <cstdio>

#include <eng/core/types/types.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/palette32.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u16;
using eng::u32;

struct MockBackend {
	const u16* taken = nullptr;
	const u16* installed = nullptr;
	unsigned installs = 0;
	void takeover_display(const u16* words) { taken = words; }
	void install_copper_list(const u16* words) {
		installed = words;
		++installs;
	}
};

alignas(16) eng::u8 g_chip[512 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = eng::ChipArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

/// Valor del MOVE de `reg` en la lista (0xffff/0x0000 si no aparece). Recorre pares
/// MOVE; los WAIT tienen el bit 0 de la primera word a 1.
u16 value_of(const u16* words, u16 count, u16 reg) {
	for (u16 i = 0; i + 1u < count; i += 2u) {
		const u16 w0 = words[i];
		if (w0 == 0xffffu) break;
		if ((w0 & 1u) != 0u) continue; // WAIT
		if (w0 == reg) return words[i + 1u];
	}
	return 0xdeadu;
}

} // namespace

int main() {
	// --- A) API del DoubleBuffer -------------------------------------------------
	{
		MemorySystem mem = make_memory();
		eng::copper::DoubleBuffer db;
		if (!db.begin(mem, 512u)) {
			std::printf("[FAIL] DoubleBuffer::begin fallo\n");
			return 1;
		}
		if (db.active_index() != 1u) {
			std::printf("[FAIL] active inicial=%u (esperado 1)\n", (unsigned)db.active_index());
			return 1;
		}
		if (db.active_words() == db.inactive_words() || db.active_words() == nullptr) {
			std::printf("[FAIL] los dos bloques deben existir y ser distintos\n");
			return 1;
		}
		db.flip();
		if (db.active_index() != 0u) {
			std::printf("[FAIL] flip no alterna\n");
			return 1;
		}
		MockBackend backend;
		db.takeover(backend);
		db.install(backend);
		if (backend.taken != db.active_words() || backend.installed != db.active_words()) {
			std::printf("[FAIL] takeover/install no publican el bloque activo\n");
			return 1;
		}
		// begin rechaza tamano inservible.
		eng::copper::DoubleBuffer bad;
		if (bad.begin(mem, 2u)) {
			std::printf("[FAIL] begin acepto un bloque de 2 bytes\n");
			return 1;
		}
	}

	// --- B) Integracion con TileScrollScene --------------------------------------
	{
		using namespace eng::graphics::drivers;
		using Scene = TileScrollScene<TileScrollMode::ehb()>;

		MemorySystem mem = make_memory();
		static const eng::Palette32 pal {};
		TileScrollConfig cfg {};
		cfg.base_palette = &pal;
		cfg.copper_bytes = 1536u;

		Scene scene;
		if (!scene.init(mem, cfg)) {
			std::printf("[FAIL] TileScrollScene::init fallo\n");
			return 1;
		}
		const u16 count = scene.copper_words();
		if (count == 0u) {
			std::printf("[FAIL] copperlist vacia\n");
			return 1;
		}

		// Primer build: AMBOS bloques llevan la lista completa (arranca en scroll 0).
		MockBackend backend;
		scene.install(backend);
		const u16* first = backend.installed;
		const u16 bplcon1_0 = value_of(first, count, static_cast<u16>(eng::copper::Register::BPLCON1));
		const u16 bplpt_0 =
			value_of(first, count, eng::copper::bitplane_pointer_low_register(0));
		if (bplcon1_0 == 0xdeadu || bplpt_0 == 0xdeadu) {
			std::printf("[FAIL] la lista no contiene BPLCON1/BPL1PTL\n");
			return 1;
		}

		// Scroll con otro `fine` (40 & 15 = 8 -> nibble 8) -> BPLCON1 debe cambiar.
		// (Ojo: el driver clampa el scroll minimo a 1, asi que scroll 0 y 33 dan el
		// mismo `fine` = 1; ver `clamp_scroll(..., 1u)` en compute_display.)
		if (!scene.rebuild_copper(40u)) {
			std::printf("[FAIL] rebuild_copper(40) fallo\n");
			return 1;
		}
		scene.install(backend);
		const u16* second = backend.installed;
		if (second == first) {
			std::printf("[FAIL] el rebuild no publico el OTRO bloque (no hay doble buffer)\n");
			return 1;
		}
		const u16 bplcon1_1 = value_of(second, count, static_cast<u16>(eng::copper::Register::BPLCON1));
		const u16 bplpt_1 =
			value_of(second, count, eng::copper::bitplane_pointer_low_register(0));
		if (bplcon1_1 == bplcon1_0) {
			std::printf("[FAIL] BPLCON1 no cambio con el scroll (0x%04x)\n", (unsigned)bplcon1_1);
			return 1;
		}
		if (bplpt_1 == bplpt_0) {
			std::printf("[FAIL] el puntero BPL1PTL no cambio con el scroll\n");
			return 1;
		}

		// Invariante: el bloque que dejo de ser activo conserva los valores viejos.
		if (value_of(first, count, static_cast<u16>(eng::copper::Register::BPLCON1)) != bplcon1_0 ||
		    value_of(first, count, eng::copper::bitplane_pointer_low_register(0)) != bplpt_0) {
			std::printf("[FAIL] el bloque inactivo fue modificado (se parcheo la lista activa)\n");
			return 1;
		}
	}

	std::printf("OK: copper::DoubleBuffer (2 bloques, flip, publicar) y TileScrollScene lo usa patchando el inactivo.\n");
	return 0;
}
