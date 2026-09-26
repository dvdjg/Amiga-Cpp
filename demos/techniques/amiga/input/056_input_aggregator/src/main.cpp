// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/input/056_input_aggregator --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/input/056_input_aggregator --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/input/056_input_aggregator --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/input/056_input_aggregator --keep-running

// ============================================================================
// Demo 056: "input aggregator" — entrada unificada (joystick + fuego + teclado).
// ============================================================================
//
// Demuestra el paso 6 de ENGINE_DESIGN.md §5: el backend rellena un
// `eng::input::InputAggregator` cada frame (direcciones por JOYxDAT de Denise +
// fuego por CIAAPRA, teclado sintético por memoria) y la lógica de juego lo
// consume SIN tocar hardware.
//
// Qué muestra: una cruz que se mueve con el joystick (puerto 0) sobre un fondo
// degradado arcoíris por Copper. Mantener FIRE la pone roja; una tecla 1..9
// (inyectada por el host vía `--automation-key`) cambia su color de forma
// determinista. Así se valida la entrada sin pulsar teclas físicamente.

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/platform/amiga/input_poll.hpp>

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

// Tecla sintética inyectada por el host (automatización por memoria de WinUAE:
// scancode Amiga, '1'=0x02..; 0xff = sin tecla). Ámbito global + enlace C (sin
// mangle) para que el runner resuelva su dirección y le escriba el `poke`.
extern "C" {
__attribute__((used)) volatile eng::u8 g_automation_keycode = 0xffu;
}

// Gancho directo 1..9 que el runner `--automation-key N` inyecta por memoria
// (misma vía que la demo 107): selecciona el color de la cruz de forma
// determinista (1..9 → índice 0..8). Se consume y se pone a 0 cada frame.
extern "C" {
__attribute__((used)) volatile eng::u8 g_tech_new = 0;
}

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

constexpr eng::u16 kScreenW = 320;
constexpr eng::u16 kScreenH = 256;

constexpr eng::u8  kRainbowLen = 32;
constexpr eng::u8  kBands = 16;          // 16 franjas de 16 líneas
constexpr eng::u16 kBandTop0 = 40;       // primera línea visible
constexpr eng::u16 kBandHeight = 16;

// Colores brillantes para la cruz (teclas 1..9 → índice 0..8).
constexpr eng::u16 kCrossColors[9] = {
	0xfff, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfa0, 0xf40,
};
constexpr eng::u8 kCrossColorCount = 9;

// Arcoíris RGB444 para el degradado de fondo (COLOR00 por franja).
constexpr eng::u16 kRainbow[kRainbowLen] = {
	0xf00, 0xf40, 0xf80, 0xfb0, 0xff0, 0xcf0, 0x8f0, 0x4f0,
	0x0f0, 0x0f4, 0x0f8, 0x0fc, 0x0ff, 0x0cf, 0x08f, 0x04f,
	0x00f, 0x40f, 0x80f, 0xc0f, 0xf0f, 0xf0c, 0xf08, 0xf04,
	0xf00, 0xc00, 0x800, 0x400, 0x000, 0x222, 0x444, 0x666,
};

struct InputAggregatorDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005601u);
			return;
		}

		m_bitplane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(2048, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005602u);
			return;
		}

		// Paleta base: color 1 = cruz (blanca al arrancar), resto = grises para
		// dejar visible el degradado de fondo (COLOR00).
		for (eng::u8 i = 0; i < 32; ++i) {
			m_palette[i] = static_cast<eng::u16>(i * 0x111u);
		}
		m_palette[0] = 0x000;
		m_palette[1] = kCrossColors[0];

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005603u);
			return;
		}

		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		// 1) Entrada: el backend rellena el agregador (joystick + fuego) y el
		//    teclado sintético se sondea aparte; el ratón se lee por delta.
		eng::input::InputAggregator agg;
		eng::amiga::poll_input(agg);
		eng::amiga::KeyboardState kbd;
		eng::amiga::poll_keyboard(kbd);
		eng::amiga::MousePollState mps = m_mouse_poll;
		eng::input::MouseState mouse;
		eng::amiga::poll_mouse(mouse, mps);
		m_mouse_poll = mps;

		// 2) Lógica: mover la cruz con el pad 0 y con el ratón (delta).
		if (agg.pad0.left)  { if (m_cx > 4u) m_cx -= 2u; }
		if (agg.pad0.right) { if (m_cx < kScreenW - 4u) m_cx += 2u; }
		if (agg.pad0.up)    { if (m_cy > 4u) m_cy -= 2u; }
		if (agg.pad0.down)  { if (m_cy < kScreenH - 4u) m_cy += 2u; }
		if (mouse.dx > 0 && m_cx < kScreenW - 4u) m_cx += 2u;
		if (mouse.dx < 0 && m_cx > 4u) m_cx -= 2u;
		if (mouse.dy < 0 && m_cy > 4u) m_cy -= 2u;
		if (mouse.dy > 0 && m_cy < kScreenH - 4u) m_cy += 2u;

		// Color: tecla 1..9 selecciona un color determinista; FIRE = rojo.
		if (kbd.pending >= 0x02u && kbd.pending <= 0x0au) {
			m_color_idx = static_cast<eng::u8>(kbd.pending - 0x02u);
		}
		const eng::u8 want = g_tech_new;
		if (want >= 1u && want <= 9u) {
			g_tech_new = 0;
			m_color_idx = want - 1u;
		}
		eng::u16 cross = kCrossColors[m_color_idx];
		if (agg.pad0.fire) {
			cross = 0xf00u;
		}
		m_palette[1] = cross;

		// 3) Dibujo: limpiar el plano 0 (color 1) y pintar la cruz en la nueva
		//    posición.
		clear_plane0();
		draw_cross(m_bitplane_block.view.data(), m_cx, m_cy);

		// 4) Parchear SOLO el valor de COLOR01 en la copperlist ya construida (estructura
		//    fija): no se reconstruye ni se reinstala la lista cada frame.
		reinterpret_cast<eng::u16*>(m_copper_block.view.data())[m_color1_word] = cross;
		(void)backend;
		(void)context;
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Pinta un píxel en el plano 0 (color 1). El resto de planos quedan a 0, así
	/// que la cruz usa exactamente el color 1 de la paleta.
	void set_pixel(eng::u8* plane0, eng::u16 x, eng::u16 y) {
		if (x >= kScreenW || y >= kScreenH) {
			return;
		}
		plane0[static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u)]
			|= static_cast<eng::u8>(1u << (7u - (x & 7u)));
	}

	void fill_rect(eng::u8* plane0, eng::s16 x0, eng::s16 y0, eng::s16 w, eng::s16 h) {
		for (eng::s16 y = y0; y < y0 + h; ++y) {
			for (eng::s16 x = x0; x < x0 + w; ++x) {
				if (x >= 0 && y >= 0) {
					set_pixel(plane0, static_cast<eng::u16>(x), static_cast<eng::u16>(y));
				}
			}
		}
	}

	void draw_cross(eng::u8* plane0, eng::u16 cx, eng::u16 cy) {
		fill_rect(plane0, static_cast<eng::s16>(cx) - 2, static_cast<eng::s16>(cy) - 20, 5, 41);
		fill_rect(plane0, static_cast<eng::s16>(cx) - 20, static_cast<eng::s16>(cy) - 2, 41, 5);
	}

	void clear_plane0() {
		eng::u16* words = reinterpret_cast<eng::u16*>(m_bitplane_block.view.data());
		for (eng::u32 i = 0; i < kPlaneBytes / 2u; ++i) {
			words[i] = 0;
		}
	}

	void build_bands() {
		for (eng::u8 b = 0; b < kBands; ++b) {
			eng::graphics::CopperIntent& it = m_intents[b];
			it.kind = eng::graphics::CopperIntentKind::PaletteLine;
			it.top = static_cast<eng::u16>(kBandTop0 + static_cast<eng::u16>(b) * kBandHeight);
			it.bottom = it.top;
			it.hpos = 0;
			it.colors = kRainbow; it.first = static_cast<eng::u8>((static_cast<eng::u8>(b * 2u) + m_phase) & (kRainbowLen - 1u));
			it.first = 0;
			it.count = 1;
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplane_block.view, kPlaneBytes
		);
		// Paleta: emitimos los MOVEs uno a uno para guardar el indice de la palabra de
		// valor de COLOR01, que es lo unico que cambia por frame (color de la cruz).
		for (eng::u8 i = 0; i < 32u; ++i) {
			const eng::u16 idx = sched.move_at(
				static_cast<eng::u16>(eng::copper::Register::COLOR00) + static_cast<eng::u16>(i) * 2u,
				m_palette[i]);
			if (i == 1u) {
				m_color1_word = static_cast<eng::u16>(idx + 1u); // palabra de valor del MOVE
			}
		}
		build_bands();
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
	eng::u8 m_color_idx = 0;
	eng::u16 m_cx = kScreenW / 2u;
	eng::u16 m_cy = kScreenH / 2u;
	eng::amiga::MousePollState m_mouse_poll {};
	eng::u16 m_palette[32] {};
	eng::u16 m_color1_word = 0; // indice de la palabra de valor del MOVE COLOR01
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::graphics::CopperIntent m_intents[kBands] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	InputAggregatorDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
