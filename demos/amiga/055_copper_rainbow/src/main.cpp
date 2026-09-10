// ============================================================================
// Demo 055: "copper rainbow" — degradado vertical animado vía CopperIntent.
// ============================================================================
//
// Demuestra el vocabulario de intenciones del engine (paso 3 de ENGINE_DESIGN.md
// §5, "retained scene + actor con CopperIntent"): la escena describe QUÉ quiere
// (una lista de `CopperIntent` con un cambio de COLOR00 por franja) y el
// `CopperScheduler` lo materializa en WAIT/MOVE de la copperlist. Es el patrón
// retained: el juego no escribe registros; solo actualiza intenciones.
//
// Qué muestra: 12 bandas horizontales (franjas de 16 líneas) que colorean el
// fondo con un degradado arcoíris. En `update()` la fase avanza y el arcoíris
// se desplaza verticalmente (reconstruyendo la copperlist por frame), para que
// la animación sea evidente.
//
// Fondo EHB estático (6 planos a cero → COLOR00 = color de la franja).

#include <eng/core/types.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/engine.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u8  kRainbowLen = 32;
constexpr eng::u8  kBands = 12;          // 12 franjas de 16 líneas
constexpr eng::u16 kBandTop0 = 40;       // primera línea visible
constexpr eng::u16 kBandHeight = 16;

// Arcoíris RGB444 (rojo → naranja → amarillo → verde → cian → azul → púrpura).
constexpr eng::u16 kRainbow[kRainbowLen] = {
	0xf00, 0xf40, 0xf80, 0xfb0, 0xff0, 0xcf0, 0x8f0, 0x4f0,
	0x0f0, 0x0f4, 0x0f8, 0x0fc, 0x0ff, 0x0cf, 0x08f, 0x04f,
	0x00f, 0x40f, 0x80f, 0xc0f, 0xf0f, 0xf0c, 0xf08, 0xf04,
	0xf00, 0xc00, 0x800, 0x400, 0x000, 0x222, 0x444, 0x666,
};

// Paleta base: COLOR00=negro (el fondo lo pintan las franjas), resto grises.
constexpr eng::graphics::drivers::EhbPalette kBasePalette {{
	0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0x888,
	0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff, 0x000,
}};

struct CopperRainbowDemo {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005501u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate(2048, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005502u);
			return;
		}
		m_bitplanes = static_cast<eng::u8*>(m_bitplane_block.data);

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005503u);
			return;
		}

		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		// Fase avanza cada frame: el arcoíris se desplaza verticalmente.
		m_phase = static_cast<eng::u8>(context.frame.frame_index & (kRainbowLen - 1u));
		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Construye la lista de `CopperIntent`: una `PaletteLine` por franja que
	/// cambia COLOR00 al color del arcoíris (desplazado por la fase).
	void build_intents() {
		for (eng::u8 b = 0; b < kBands; ++b) {
			eng::graphics::CopperIntent& it = m_intents[b];
			it.kind = eng::graphics::CopperIntentKind::PaletteLine;
			it.top = static_cast<eng::u16>(kBandTop0 + static_cast<eng::u16>(b) * kBandHeight);
			it.bottom = it.top;
			it.hpos = 0;
			it.colors = &kRainbow[(static_cast<eng::u8>(b * 2u) + m_phase) & (kRainbowLen - 1u)];
			it.first = 0;
			it.count = 1;
		}
	}

	bool build_copper() {
		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplanes, kPlaneBytes
		);
		sched.emit_palette(kBasePalette.color);
		build_intents();
		sched.emit_copper_intents(m_intents, kBands);
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	eng::u8 m_phase = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u8* m_bitplanes = nullptr;
	eng::MemoryBlock m_bitplane_block {};
	eng::MemoryBlock m_copper_block {};
	eng::graphics::CopperIntent m_intents[kBands] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	CopperRainbowDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
