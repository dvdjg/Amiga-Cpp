#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/mode_switch.hpp>
#include <eng/platform/amiga_minimal.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

/// Microtest MI09: campo **EHB** (6 planos, BPLCON4=1) arriba + HUD de **4 planos**
/// sin EHB abajo, conmutado por `ModeSwitchZone`. Es el uso real de 201: al bajar
/// al HUD se reprograma BPLCON0 (BPU=4) y BPLCON4=0, o el bit half-brite mezclaria
/// colores. El test comprueba que el campo muestra >32 colores (EHB) y el HUD se
/// queda en 16 (sus 4 planos).
struct DemoGame {
	static constexpr eng::u16 width = 320;
	static constexpr eng::u16 height = 256;
	static constexpr eng::u16 bytes_per_row = width / 8;
	static constexpr eng::u32 plane_bytes = static_cast<eng::u32>(bytes_per_row) * height;
	static constexpr eng::u16 diwstrt_y = 0x2cu;
	static constexpr eng::u16 ddfstrt = 0x0038u;
	static constexpr eng::u16 ddfstop = 0x00d0u;
	static constexpr eng::u8 field_planes = 6u; // EHB
	static constexpr eng::u8 hud_planes = 4u;
	static constexpr eng::u16 hud_line = 160u;  // raster 0x2c+160 = 204

	static constexpr eng::u16 colors[32] {
		0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfff,
		0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0xaaa, 0x444,
		0xf80, 0x8f0, 0x08f, 0xf08, 0x80f, 0x0f8, 0xc44, 0x4c4,
		0x44c, 0xcc4, 0xc4c, 0x4cc, 0xe86, 0x6e8, 0x86e, 0x222,
	};

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({112u * 1024u, 8u * 1024u, 4u * 1024u});

		m_plane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(
			plane_bytes * (field_planes + hud_planes), 16);
		m_copper_block = backend.memory().chip.allocate(2048u, 16);
		if (!m_plane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000115u);
			return;
		}
		eng::u8* const planes = m_plane_block.view.data();
		const eng::u32 hud_off = static_cast<eng::u32>(field_planes) * plane_bytes;
		paint(planes + 0u, 0u, hud_line, field_planes);
		paint(planes + hud_off, hud_line, height, hud_planes);

		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, ddfstrt, ddfstop, bytes_per_row,
			static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(field_planes) << 12u)),
			field_planes,
			eng::PlaneBytes { planes, static_cast<eng::usize>(field_planes) * plane_bytes },
			plane_bytes);
		sched.move(eng::copper::Register::BPLCON4, 0x0001u); // EHB en el campo
		sched.emit_palette(eng::PaletteWords { colors, 32u });

		eng::graphics::ModeSwitchZone z {};
		z.top = static_cast<eng::u16>(diwstrt_y + hud_line);
		z.bplcon0 = static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(hud_planes) << 12u));
		z.set_bplcon4 = true; z.bplcon4 = 0u; // salir de EHB en el HUD
		z.set_bplcon1 = true; z.bplcon1 = 0u;
		z.ddfstrt = ddfstrt; z.ddfstop = ddfstop;
		z.bpl1mod = 0x0000u; z.bpl2mod = 0x0000u;
		z.planes = hud_planes;
		z.plane_bytes = plane_bytes;
		z.bitplanes = eng::PlaneViewBytes { planes + hud_off, static_cast<eng::usize>(hud_planes) * plane_bytes };
		z.palette = eng::PaletteWords { colors, static_cast<eng::usize>(1u << hud_planes) };
		z.palette_colors = static_cast<eng::u8>(1u << hud_planes);
		const bool zone_ok = sched.emit_mode_switch_zone(z);

		sched.end();
		m_copper_ok = zone_ok && sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		if (m_memory_ok && m_copper_ok) {
			backend.takeover_display(m_copper_ptr);
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000116u);
		}
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_copper_ok) backend.install_copper_list(m_copper_ptr);
	}
	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_copper_ok && m_copper_words > 0) backend.install_copper_list(m_copper_ptr);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Barras solidas por linea: el indice recorre 0..2^planes-1 en [l0,l1); la
	/// primera linea de la zona es la fila 0 del buffer (el DMA arranca ahi).
	static void paint(eng::u8* planes, eng::u16 l0, eng::u16 l1, eng::u8 nplanes) {
		const eng::u16 n = static_cast<eng::u16>(1u << nplanes);
		const eng::u16 h = static_cast<eng::u16>(l1 - l0);
		for (eng::u16 y = 0; y < h; ++y) {
			const eng::u8 index = static_cast<eng::u8>((static_cast<eng::u32>(y) * n) / h);
			const eng::u32 row = static_cast<eng::u32>(y) * bytes_per_row;
			for (eng::u16 bx = 0; bx < bytes_per_row; ++bx) {
				for (eng::u8 p = 0; p < nplanes; ++p) {
					planes[static_cast<eng::u32>(p) * plane_bytes + row + bx] =
						(index & (1u << p)) ? 0xffu : 0x00u;
				}
			}
		}
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_plane_block {};
	eng::MemoryBlock m_copper_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);
	return 0;
}
