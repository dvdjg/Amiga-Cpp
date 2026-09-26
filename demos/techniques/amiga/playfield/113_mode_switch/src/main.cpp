// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/113_mode_switch --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/113_mode_switch --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/113_mode_switch --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/113_mode_switch --keep-running

#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/mode_switch.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

#ifndef K_NO_MODE_SWITCH
#define K_NO_MODE_SWITCH 0
#endif

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

/// Microtest de Fase 1b: `ModeSwitchZone` en hardware (invariante MI09).
///
/// La pantalla se parte en dos tramos con **geometría distinta**:
///
///   ┌──────────────────────────────┐  y=0    (raster 0x2c)
///   │ CAMPO  5 planos (32 colores)  │
///   │ reticula de celdas de color   │
///   ├──────────────────────────────┤  y=160  (raster 0xcc)  <- WAIT + ModeSwitchZone
///   │ HUD    2 planos (4 colores)   │
///   │ barras que reciclan 4 colores │
///   └──────────────────────────────┘  y=256
///
/// El campo usa 5 planos interleaved-contiguos (mod=0, layout contiguo) y el
/// HUD cambia, en el raster de corte, `BPLCON0` (BPU 5 -> 2) y los punteros, con el
/// orden canónico MI09. Detrás de los 2 planos del HUD se colocan 3 planos "veneno"
/// a 0xFF: si la conmutación no se aplica y el HUD siguiera leyendo 5 planos, las
/// barras mostrarían bits altos (colores > 3), detectable por el analizador.
struct DemoGame {
	static constexpr eng::u16 width = 320;
	static constexpr eng::u16 height = 256;
	static constexpr eng::u16 bytes_per_row = width / 8;
	static constexpr eng::u32 plane_bytes = static_cast<eng::u32>(bytes_per_row) * height;

	static constexpr eng::u8 field_planes_count = 5;
	static constexpr eng::u8 hud_planes_count = 2;
	static constexpr eng::u8 poison_planes_count = 3;

	// Paleta del campo: 32 colores RGB444 vivos.
	static constexpr eng::u16 field_palette[32] {
		0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfff,
		0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0xaaa, 0x444,
		0xf80, 0x8f0, 0x08f, 0xf08, 0x80f, 0x0f8, 0xc44, 0x4c4,
		0x44c, 0xcc4, 0xc4c, 0x4cc, 0xe86, 0x6e8, 0x86e, 0x222,
	};
	// Paleta del HUD: solo 4 colores (2 planos). Distinta de los primeros del campo.
	static constexpr eng::u16 hud_palette[4] {
		0x000, 0xe00, 0x0e0, 0x00e,
	};

	// Linea de corte: raster 0xcc (= viewport 160 desde DIWSTRT 0x2c).
	static constexpr eng::u16 hud_raster = 0xcc;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			112u * 1024u, // Chip: 10 planos (campo+HUD+veneno) + copperlist.
			8u * 1024u,   // Slow: metadatos.
			4u * 1024u,   // Frame scratch.
		});

		// Un unico bloque de planos: [campo 0..4][hud 0..1][veneno 0..2].
		m_plane_block = backend.memory().chip.allocate_block<eng::PlaneTag>(
			static_cast<eng::u32>(bytes_per_row) * height *
				(field_planes_count + hud_planes_count + poison_planes_count), 16);
		m_copper_block = backend.memory().chip.allocate_block<eng::CopperTag>(2048u, 16);

		if (!m_plane_block.valid() || !m_copper_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000050u);
			return;
		}

		eng::u8* const planes = m_plane_block.view.data();
		const eng::u32 field_off = 0u;
		const eng::u32 hud_off = static_cast<eng::u32>(field_planes_count) * plane_bytes;
		const eng::u32 poison_off = hud_off + static_cast<eng::u32>(hud_planes_count) * plane_bytes;

		build_field(planes + field_off);
		build_hud(planes + hud_off);
		fill_poison(planes + poison_off);

		// Copperlist: display de 5 planos + paleta del campo + ModeSwitchZone a 2.
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0, bytes_per_row,
			static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(field_planes_count) << 12u)),
			field_planes_count,
			eng::PlaneBytes { planes + field_off, static_cast<eng::usize>(field_planes_count) * plane_bytes },
			plane_bytes);
		sched.emit_palette(eng::PaletteWords { field_palette, 32u });

		bool zone_ok = true;
#if !K_NO_MODE_SWITCH
		eng::graphics::ModeSwitchZone zone {};
		zone.top = hud_raster;
		zone.bplcon0 = static_cast<eng::u16>(0x0200u | (static_cast<eng::u16>(hud_planes_count) << 12u));
		zone.ddfstrt = 0x0038u;
		zone.ddfstop = 0x00d0u;
		zone.bpl1mod = 0x0000u;
		zone.bpl2mod = 0x0000u;
		zone.planes = hud_planes_count;
		zone.plane_bytes = plane_bytes;
		zone.bitplanes = eng::PlaneViewBytes { planes + hud_off, static_cast<eng::usize>(hud_planes_count) * plane_bytes };
		zone.palette = eng::PaletteWords { hud_palette, 4u };
		zone.palette_colors = 4u;
		zone_ok = sched.emit_mode_switch_zone(zone);
#endif

		sched.end();
		m_copper_ok = zone_ok && sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();

		if (m_memory_ok && m_copper_ok) {
			backend.takeover_display(m_copper_ptr);
			eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
		} else {
			eng::debug::mark_failed(g_eng_run_status, 0x00000051u);
		}
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_copper_ok && m_copper_words > 0) {
			backend.install_copper_list(m_copper_ptr);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Reticula de celdas de 40 px: el indice usa hasta 5 bits (0..31) en bandas
	/// de 32 lineas, de modo que el campo muestra muchos colores distintos.
	static void build_field(eng::u8* planes) {
		for (eng::u16 y = 0; y < height; ++y) {
			const eng::u8 band = static_cast<eng::u8>((y >> 5u) & 3u);
			const eng::u32 row = static_cast<eng::u32>(y) * bytes_per_row;
			for (eng::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
				const eng::u8 cell = static_cast<eng::u8>((byte_x / 5u) & 7u);
				const eng::u8 index = static_cast<eng::u8>((band << 3u) | cell);
				write_cell(planes, row + byte_x, index, field_planes_count);
			}
		}
	}

	/// Barras del HUD que reciclan solo 4 indices (2 planos).
	static void build_hud(eng::u8* planes) {
		for (eng::u16 y = 0; y < height; ++y) {
			const eng::u32 row = static_cast<eng::u32>(y) * bytes_per_row;
			for (eng::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
				const eng::u8 index = static_cast<eng::u8>((byte_x / 5u) & 3u);
				write_cell(planes, row + byte_x, index, hud_planes_count);
			}
		}
	}

	static void fill_poison(eng::u8* planes) {
		eng::u8* end = planes + static_cast<eng::u32>(poison_planes_count) * plane_bytes;
		for (eng::u8* p = planes; p < end; ++p) {
			*p = 0xffu;
		}
	}

	static void write_cell(eng::u8* planes, eng::u32 byte_index, eng::u8 index, eng::u8 count) {
		for (eng::u8 plane = 0; plane < count; ++plane) {
			planes[static_cast<eng::u32>(plane) * plane_bytes + byte_index] =
				(index & (1u << plane)) ? 0xffu : 0x00u;
		}
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::PlaneTag> m_plane_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
