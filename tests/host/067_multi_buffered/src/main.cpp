// ============================================================================
// Test HOST-067: `MultiBuffered<Driver, N>` (N buffers de display + swap COP1LC)
// ============================================================================
//
// Valida en host (g++ nativo, sin WinAmiga) la abstraccion que generaliza el patron
// "2 buffers + install tras VBlank" que las demos 061/080/082/083 repetian a mano:
//
//   1) `init` reserva N parejas (planos + copperlist) y enlaza cada driver (`bind`).
//   2) Con N>1 arranca escribiendo en el slot 1: el 0 es el que se muestra, asi el
//      primer frame no se dibuja sobre lo visible.
//   3) `commit` instala la copperlist del buffer en escritura y rota (0->1->0 con N=2).
//   4) `takeover` muestra el slot 0.
//   5) N=1 equivale a driver suelto (back siempre 0, sin flip efectivo).
//   6) Integracion real: `PlanarScene` con N=2 produce dos copperlists distintas (cada una
//      apunta a su propio bitmap), que es lo que demuestra la separacion
//      "emision de copperlist" / "bloque de planos" (`bind`).
//
// El backend es de pega (solo registra que copperlist se toma/instala): nada de
// hardware ni RAM Amiga.
//
// Ejecucion:
//   bash tools/run-host-tests.sh tests/host/067_multi_buffered

#include <cstdio>

#include <eng/core/types.hpp>
#include <eng/graphics/driver.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/drivers/planar_scene.hpp>
#include <eng/graphics/drivers/multi_buffered.hpp>
#include <eng/memory/arena.hpp>

namespace {

using eng::MemoryKind;
using eng::MemorySystem;
using eng::LinearArena;
using eng::u16;
using eng::u32;

/// Backend de pega: registra la ultima copperlist tomada e instalada.
struct MockBackend {
	const eng::u16* taken = nullptr;
	const eng::u16* installed = nullptr;
	unsigned installs = 0;
	void takeover_display(const eng::u16* words) { taken = words; }
	void install_copper_list(const eng::u16* words) {
		installed = words;
		++installs;
	}
};

/// Driver de pega: solo geometria y contabilidad (sin hardware). Cada slot guarda sus
/// propios bloques; la "copperlist" es el propio bloque (su puntero).
struct StubDriver {
	struct Config {
		u32 bitplane_bytes = 64;
		u32 copper_bytes = 16;
	};
	static constexpr u32 bitplane_bytes_for(const Config& c) { return c.bitplane_bytes; }

	bool bind(eng::Block<eng::PlaneTag> p, eng::Block<eng::CopperTag> c, const Config&) {
		m_planes = p;
		m_copper = c;
		return m_planes.valid() && m_copper.valid();
	}
	template <class Backend>
	void takeover(Backend& b) const {
		b.takeover_display(reinterpret_cast<const u16*>(m_copper.view.data()));
	}
	template <class Backend>
	void install(Backend& b) const {
		b.install_copper_list(reinterpret_cast<const u16*>(m_copper.view.data()));
	}
	eng::PlaneBytes bitplanes() const { return m_planes.view; }
	const eng::u16* copper_ptr() const { return reinterpret_cast<const u16*>(m_copper.view.data()); }

	eng::Block<eng::PlaneTag> m_planes {};
	eng::Block<eng::CopperTag> m_copper {};
};

alignas(16) eng::u8 g_chip[256 * 1024];

MemorySystem make_memory() {
	MemorySystem mem;
	mem.chip = LinearArena {g_chip, sizeof(g_chip), MemoryKind::Chip};
	return mem;
}

} // namespace

int main() {
	// --- 1..5) Logica del wrapper con un driver de pega, N=2 y N=1 ---------------
	{
		MemorySystem mem = make_memory();
		eng::graphics::drivers::MultiBuffered<StubDriver, 2> mb {};
		StubDriver::Config cfg {};
		if (!mb.init(mem, cfg)) {
			std::printf("[FAIL] init N=2 fallo\n");
			return 1;
		}
		if (mb.back_slot() != 1u) {
			std::printf("[FAIL] back_slot inicial = %u (esperado 1: no escribir lo visible)\n",
				    (unsigned)mb.back_slot());
			return 1;
		}
		if (mb.slot(0).bitplanes().data() == mb.slot(1).bitplanes().data()) {
			std::printf("[FAIL] los slots comparten el bloque de planos\n");
			return 1;
		}
		if (mb.slot(0).copper_ptr() == mb.slot(1).copper_ptr()) {
			std::printf("[FAIL] los slots comparten la copperlist\n");
			return 1;
		}

		MockBackend backend;
		mb.takeover(backend);
		if (backend.taken != mb.slot(0).copper_ptr()) {
			std::printf("[FAIL] takeover no muestra el slot 0\n");
			return 1;
		}

		// commit instala el buffer en escritura y rota.
		mb.commit(backend);
		if (backend.installed != mb.slot(1).copper_ptr() || mb.back_slot() != 0u) {
			std::printf("[FAIL] commit 1: installed=%p back=%u\n", (const void*)backend.installed,
				    (unsigned)mb.back_slot());
			return 1;
		}
		mb.commit(backend);
		if (backend.installed != mb.slot(0).copper_ptr() || mb.back_slot() != 1u) {
			std::printf("[FAIL] commit 2: no rota 0->1\n");
			return 1;
		}
		if (backend.installs != 2u) {
			std::printf("[FAIL] installs=%u (esperado 2)\n", backend.installs);
			return 1;
		}
	}
	{
		MemorySystem mem = make_memory();
		eng::graphics::drivers::MultiBuffered<StubDriver, 1> mb {};
		if (!mb.init(mem, StubDriver::Config {})) {
			std::printf("[FAIL] init N=1 fallo\n");
			return 1;
		}
		if (mb.back_slot() != 0u) {
			std::printf("[FAIL] N=1: back_slot=%u (esperado 0)\n", (unsigned)mb.back_slot());
			return 1;
		}
		MockBackend backend;
		mb.commit(backend);
		if (backend.installed != mb.slot(0).copper_ptr() || mb.back_slot() != 0u) {
			std::printf("[FAIL] N=1: commit debe quedarse en el slot 0\n");
			return 1;
		}
	}

	// --- 6) Integracion con un driver real: PlanarScene, N=2 ------------------------
	{
		using eng::graphics::drivers::PlanarScene;
		using eng::graphics::drivers::PlanarSceneConfig;
		MemorySystem mem = make_memory();

		PlanarSceneConfig cfg {};
		cfg.rows = 64;
		cfg.planes = 4;
		cfg.bytes_per_row = 40;
		cfg.bplcon0 = 0x4200u;
		cfg.row_repeat = 4;
		cfg.bplcon1_shift = 0u;

		eng::graphics::drivers::MultiBuffered<PlanarScene, 2> scenes {};
		if (!scenes.init(mem, cfg)) {
			std::printf("[FAIL] MultiBuffered<PlanarScene,2>::init fallo\n");
			return 1;
		}
		const u32 planebytes = PlanarScene::plane_bytes_for(cfg);
		if (scenes.slot(0).plane_bytes() != planebytes || scenes.slot(1).plane_bytes() != planebytes) {
			std::printf("[FAIL] plane_bytes=%u (esperado %u)\n", (unsigned)scenes.slot(0).plane_bytes(),
				    (unsigned)planebytes);
			return 1;
		}
		if (scenes.slot(0).bitplanes().data() == scenes.slot(1).bitplanes().data()) {
			std::printf("[FAIL] PlanarScene: los dos slots comparten bitmap\n");
			return 1;
		}
		// Las dos listas deben diferir: cada una apunta a su propio bitmap (BPLxPT).
		const u16* c0 = scenes.slot(0).copper_words_ptr();
		const u16* c1 = scenes.slot(1).copper_words_ptr();
		const u16 n0 = scenes.slot(0).copper_words();
		const u16 n1 = scenes.slot(1).copper_words();
		if (n0 == 0u || n1 == 0u) {
			std::printf("[FAIL] copperlist vacia (n0=%u n1=%u)\n", (unsigned)n0, (unsigned)n1);
			return 1;
		}
		unsigned diff = 0;
		const u16 n = (n0 < n1) ? n0 : n1;
		for (u16 i = 0; i < n; ++i) {
			if (c0[i] != c1[i]) {
				++diff;
			}
		}
		if (diff == 0u) {
			std::printf("[FAIL] las copperlists de los dos slots son identicas (mismo bitmap)\n");
			return 1;
		}

		MockBackend backend;
		scenes.takeover(backend);
		if (backend.taken != c0) {
			std::printf("[FAIL] PlanarScene: takeover no usa el slot 0\n");
			return 1;
		}
		scenes.commit(backend);
		if (backend.installed != c1) {
			std::printf("[FAIL] PlanarScene: commit no instala el slot 1\n");
			return 1;
		}
	}

	// --- 7) StaticEhbScene tambien admite bind() (geometria fija) ----------------
	{
		using eng::graphics::drivers::EhbPalette;
		using eng::graphics::drivers::StaticEhbScene;
		using eng::graphics::drivers::StaticEhbSceneConfig;
		MemorySystem mem = make_memory();
		static const EhbPalette pal {};
		const StaticEhbSceneConfig cfg {&pal, nullptr, 0u, 1024u};

		eng::graphics::drivers::MultiBuffered<StaticEhbScene, 2> scenes {};
		if (!scenes.init(mem, cfg)) {
			std::printf("[FAIL] MultiBuffered<StaticEhbScene,2>::init fallo\n");
			return 1;
		}
		if (scenes.slot(0).bitplanes().data() == scenes.slot(1).bitplanes().data()) {
			std::printf("[FAIL] StaticEhbScene: los dos slots comparten bitmap\n");
			return 1;
		}
		MockBackend backend;
		scenes.takeover(backend);
		if (backend.taken != scenes.slot(0).copper_words_ptr()) {
			std::printf("[FAIL] StaticEhbScene: takeover no usa el slot 0\n");
			return 1;
		}
		scenes.commit(backend);
		if (backend.installed != scenes.slot(1).copper_words_ptr()) {
			std::printf("[FAIL] StaticEhbScene: commit no instala el slot 1\n");
			return 1;
		}
	}

	std::printf("OK: MultiBuffered (N buffers + swap COP1LC) y bind() sin dueno de memoria.\n");
	return 0;
}
