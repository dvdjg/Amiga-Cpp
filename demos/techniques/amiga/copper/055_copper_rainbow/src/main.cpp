// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/055_copper_rainbow --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/055_copper_rainbow --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/copper/055_copper_rainbow --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/copper/055_copper_rainbow --keep-running

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

#include <eng/api/api.hpp>
#include <eng/graphics/copper/plan.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/raster_intent.hpp>
#include <eng/platform/amiga/backend.hpp>

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
constexpr eng::Palette32 kBasePalette {{
	0x000, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0x888,
	0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff, 0x000,
}};

struct CopperRainbowDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
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

		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		if (!m_bitplane_block.valid() ||
		    !m_plan.begin(backend.memory(), {2048u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005502u);
			return;
		}

		if (!build_frame()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005503u);
			return;
		}

		m_plan.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_plan.words()));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		// Fase avanza cada frame: el arcoíris se desplaza verticalmente.
		m_phase = static_cast<eng::u8>(context.frame.frame_index & (kRainbowLen - 1u));
		if (build_frame()) {
			m_plan.commit(backend);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
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
			// Cada franja cambia COLOR00 al color del arcoíris desplazado por la fase.
			// En `CopperIntent`, `first` es el REGISTRO COLOR de arranque (0 = COLOR00) y
			// `colors` el tramo de paleta a escribir: aquí, una vista de 1 color sobre
			// `kRainbow[idx]`, de modo que COLOR00 = kRainbow[idx].
			const eng::u8 idx = static_cast<eng::u8>(
				(static_cast<eng::u8>(b * 2u) + m_phase) & (kRainbowLen - 1u));
			it.colors = eng::PaletteWords {kRainbow + idx, 1};
			it.first = 0;
			it.count = 1;
		}
	}

	/// Construye la lista del frame con el `copper::Plan`: parte estática (display +
	/// paleta base) + las intenciones de las franjas, ordenadas por el plan (la demo las
	/// añade en orden de banda, que ya es ascendente, pero el invariante lo garantiza el
	/// plan) + la cola. El plan escribe SIEMPRE en el buffer trasero y `commit` publica.
	bool build_frame() {
		m_plan.begin_frame();
		m_plan.scheduler().emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplane_block.view, kPlaneBytes
		);
		m_plan.scheduler().emit_palette(kBasePalette.color);
		build_intents();
		m_plan.add(m_intents, kBands);
		m_plan.materialize();
		m_plan.scheduler().wait_line(0xf8);
		m_plan.scheduler().move(eng::copper::Register::COLOR00, 0x0000);
		m_copper_ok = m_plan.end_frame();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u8 m_phase = 0;
	eng::copper::Plan m_plan {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::graphics::CopperIntent m_intents[kBands] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	CopperRainbowDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
