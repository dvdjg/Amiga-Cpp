// ============================================================================
// Demo 111 - Side-scroller horizontal (limite del scroll X) sobre XYLimited
// ============================================================================
//
// Escenario: mundo 4096 px de ancho x 320 px de alto, tiles 16x16 -> 256x20
// celdas, tileset de 128 tiles. Es un X-limited: la camara avanza en X (scroll
// largo, anillo XLimited) y el Y es CORTO (aqui fijo, `scroll_y=false`, mundo de
// 320 px con ventana de 256 -> 64 px de recorrido vertical potencial). Coste de
// Blitter proporcional al salto; framebuffer acotado.
//
//   X: AxisMode::Ring   (XLimited, anillo con banda entrante)
//   Y: scroll_y = false (sin corkscrew; el Y del mundo cabe casi entero)
//
// DPF: BG = tilemap XLimited (PF1) + FG = lienzo plano de objetos (PF2, delante).

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>

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

namespace field = eng::field;

constexpr eng::u16 kTileW = 16;
constexpr eng::u16 kTileH = 16;
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u8  kPlanes = 3;            // DPF 3+3
constexpr eng::u16 kDisplayH = 256;        // scroll_y=false -> display = viewport

// Mundo: 4096 x 320 px -> 256 x 20 tiles.
constexpr eng::u16 kMapCols = 256;
constexpr eng::u16 kMapRows = 20;
constexpr eng::u16 kTilesetCount = 128;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

eng::u16 side_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return field::demo::pf_plane_row(glyph, static_cast<eng::u8>(variant & 3u), row, plane, 0, false);
}

// Paleta DPF de 16: BG (PF1, 0..7) + objetos (PF2, 8..15).
constexpr eng::u16 kPalette[16] {
	0x000, 0x131, 0x242, 0x353, 0x464, 0x575, 0x686, 0x797,
	0x0cf, 0x7ef, 0xfff, 0xf40, 0x0f0, 0xfd0, 0xf0f, 0xaaa,
};

eng::u16 g_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::s16 m_ship_y = 200;
	eng::s16 m_ship_py = 200;
	struct Bullet { eng::s16 x = 0, y = 0, px = 0; bool live = false; };
	Bullet m_bullets[6] {};
	eng::u8 m_fire = 0;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011101u);
			return;
		}
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				g_map[static_cast<eng::u32>(y) * kMapCols + x] =
					static_cast<eng::u16>(field::demo::cell_hash(x, y, 0x1234u) & (kTilesetCount - 1u));
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = false;                                 // X-limited (Y fijo)
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		scene_cfg.map.wrap_x = kMapCols;    // X toroidal (scroll largo continuo)
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &side_row;
		scene_cfg.bg_row_fn = &side_row;
		scene_cfg.palette = kPalette;
		scene_cfg.dpf.enabled = true;
		scene_cfg.dpf.fg_canvas = true;
		scene_cfg.dpf.foreground_is_pf2 = true;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011102u);
			return;
		}
		scene.bg().set_camera(0, 0);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011103u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011104u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x11100000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});

		// Avance X hacia la derecha (+2 px/frame). El mapa es toroidal, no se topea.
		if (!scene.bg().update_scroll(plan, 2, 0)) {
			scene.bg().set_camera(0, 0);
		}
		if (!backend.execute_frame_plan(plan)) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011110u); return;
		}
		if (!scene.compose()) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011111u); return;
		}

		// FG: nave con vaivén vertical + balas hacia la derecha.
		const eng::s16 ship_x = 60;
		{
			auto fg = scene.canvas_fg_surface();
			fg.fill_rect(ship_x, m_ship_py, 14, 16, 0);
			for (auto& b : m_bullets) if (b.live) fg.fill_rect(b.px, b.y, 6, 2, 0);
			if (++m_ship_y >= 230) m_ship_y = 40;   // barrido vertical
			if ((++m_fire & 7u) == 0u) {
				for (auto& b : m_bullets) { if (!b.live) { b.live = true; b.x = ship_x + 16; b.y = m_ship_y + 7; break; } }
			}
			for (auto& b : m_bullets) { if (!b.live) continue; b.x = static_cast<eng::s16>(b.x + 8); if (b.x > kViewportW - 4) b.live = false; }
			// Nave (silueta) y balas.
			fg.fill_rect(static_cast<eng::s16>(ship_x + 10), static_cast<eng::s16>(m_ship_y + 4), 6, 8, 2);
			fg.fill_rect(static_cast<eng::s16>(ship_x + 6), m_ship_y, 8, 16, 1);
			fg.fill_rect(ship_x, static_cast<eng::s16>(m_ship_y + 6), 6, 4, 3);
			for (auto& b : m_bullets) if (b.live) { fg.fill_rect(b.x, b.y, 6, 2, 2); b.px = b.x; }
			m_ship_py = m_ship_y;
		}

		g_eng_run_status.detail = 0x11100000u |
			((static_cast<eng::u32>(scene.bg().mapposx()) & 0xffffu) << 8) |
			(static_cast<eng::u32>(scene.bg().mapposy()) & 0xffu);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
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
