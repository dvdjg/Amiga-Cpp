#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
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

/// Microtest MI09 ampliado: campo de 5 planos y **tres franjas apiladas** de 4, 3
/// y 2 planos, cada una con su propia `ModeSwitchZone` (mismo DDF, módulos y
/// BPLCON0 recalculados). Cubre el enunciado "HUD de 2/3/4 planos bajo un campo de
/// 5". Cada franja pinta barras solidas que recorren TODOS los indices de su
/// profundidad, de modo que el analizador puede contar colores por franja y
/// verificar 2^planos.
///
///   lineas 0..80   campo   5 planos  (32 colores)
///   lineas 80..128 zona 4 4 planos  (16 colores)
///   lineas 128..176 zona 3 3 planos (8 colores)
///   lineas 176..256 zona 2 2 planos (4 colores)
struct DemoGame {
	static constexpr eng::u16 width = 320;
	static constexpr eng::u16 height = 256;
	static constexpr eng::u16 bytes_per_row = width / 8;
	static constexpr eng::u32 plane_bytes = static_cast<eng::u32>(bytes_per_row) * height;
	static constexpr eng::u16 diwstrt_y = 0x2cu; // DIWSTRT 0x2c81
	static constexpr eng::u16 ddfstrt = 0x0038u;
	static constexpr eng::u16 ddfstop = 0x00d0u;
	static constexpr eng::u8 field_planes = 5u;

	// Paleta compartida (32 colores vivos y distintos); cada franja usa sus 2^N.
	static constexpr eng::u16 colors[32] {
		0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfff,
		0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0xaaa, 0x444,
		0xf80, 0x8f0, 0x08f, 0xf08, 0x80f, 0x0f8, 0xc44, 0x4c4,
		0x44c, 0xcc4, 0xc4c, 0x4cc, 0xe86, 0x6e8, 0x86e, 0x222,
	};

	struct Band {
		eng::u16 start_line;
		eng::u16 end_line;
		eng::u8  planes;
		eng::u32 plane_off;
	};

	// Tres franjas; la primera (campo) se emite con emit_planes_display.
	static constexpr Band field {0u, 80u, field_planes, 0u};
	static constexpr Band bands[3] {
		{80u, 128u, 4u, (field_planes + 0u) * plane_bytes},
		{128u, 176u, 3u, (field_planes + 4u) * plane_bytes},
		{176u, 256u, 2u, (field_planes + 7u) * plane_bytes},
	};

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({192u * 1024u, 8u * 1024u, 4u * 1024u});

		m_plane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(
			plane_bytes * (field_planes + 4u + 3u + 2u), 16);
		m_copper_block = backend.memory().chip.allocate(4096u, 16);
		if (!m_plane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000114u);
			return;
		}
		eng::u8* const planes = m_plane_block.view.data();
		paint_band(planes + field.plane_off, field);
		for (const Band& b : bands) paint_band(planes + b.plane_off, b);

		eng::copper::Scheduler sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, ddfstrt, ddfstop, bytes_per_row,
			static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(field_planes) << 12u)),
			field_planes,
			eng::PlaneBytes { planes + field.plane_off, static_cast<eng::usize>(field_planes) * plane_bytes },
			plane_bytes);
		sched.emit_palette(eng::PaletteWords { colors, 32u });

		// Una ModeSwitchZone por franja, en orden ascendente de raster.
		for (const Band& b : bands) {
			eng::graphics::ModeSwitchZone z {};
			z.top = static_cast<eng::u16>(diwstrt_y + b.start_line);
			z.bplcon0 = static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(b.planes) << 12u));
			z.set_bplcon1 = true; z.bplcon1 = 0u;
			z.ddfstrt = ddfstrt; z.ddfstop = ddfstop;
			// Layout SEPARADO (como `emit_planes_display`): cada plano es un bitmap
			// contiguo de `plane_bytes`, filas de 40 B; el mismo DDF -> modulos 0.
			z.bpl1mod = 0x0000u;
			z.bpl2mod = 0x0000u;
			z.planes = b.planes;
			z.plane_bytes = plane_bytes; // stride entre punteros de plano (separado)
			z.bitplanes = eng::PlaneViewBytes { planes + b.plane_off,
				static_cast<eng::usize>(b.planes) * plane_bytes };
			z.palette = eng::PaletteWords { colors, static_cast<eng::usize>(1u << b.planes) };
			z.palette_colors = static_cast<eng::u8>(1u << b.planes);
			m_zone_ok = m_zone_ok && sched.emit_mode_switch_zone(z);
		}

		sched.end();
		m_copper_ok = m_zone_ok && sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		if (m_memory_ok && m_copper_ok) {
			backend.takeover_display(m_copper_ptr);
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000115u);
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
	/// Pinta barras solidas: el indice recorre 0..2^planes-1 a lo largo de las
	/// lineas de la franja (cada fila es un color plano). El DMA arranca en el
	/// offset 0 del buffer al conmutar la zona, asi que la primera linea de la
	/// franja es la fila 0 del buffer (no la linea absoluta).
	static void paint_band(eng::u8* planes, const Band& b) {
		const eng::u16 n = static_cast<eng::u16>(1u << b.planes);
		const eng::u16 h = static_cast<eng::u16>(b.end_line - b.start_line);
			for (eng::u16 y = 0; y < h; ++y) {
			const eng::u8 index = static_cast<eng::u8>((static_cast<eng::u32>(y) * n) / h);
			const eng::u32 row = static_cast<eng::u32>(y) * bytes_per_row;
			for (eng::u16 bx = 0; bx < bytes_per_row; ++bx) {
				for (eng::u8 p = 0; p < b.planes; ++p) {
					planes[static_cast<eng::u32>(p) * plane_bytes + row + bx] =
						(index & (1u << p)) ? 0xffu : 0x00u;
				}
			}
		}
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	bool m_zone_ok = true;
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

