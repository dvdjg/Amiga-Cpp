// ============================================================================
// Demo 110 - Shooter vertical (limite del scroll Y) sobre XYLimited
// ============================================================================
//
// Escenario: mundo 400 px de ancho x 2048 px de alto, tiles 16x16 -> 25x128
// celdas, tileset de 128 tiles. Es un Y-limited: la camara avanza hacia ARRIBA
// (la nave no vuelve a bajar) con el anillo vertical del corkscrew; el X es
// FINITO (recorrido 0..80, sin anillo ni bandas de guarda laterales). Coste de
// Blitter proporcional al salto; el framebuffer esta acotado (el mapa solo
// ocupa indices y el tileset).
//
//   X: AxisMode::Finite  (puntero directo; no repinta)
//   Y: scroll_y = true   (anillo corkscrew) + ScrollMode::OneDirection
//
// El FG de objetos (DPF) se anade en una fase posterior; esta demo es el BG.

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
constexpr eng::u8  kPlanes = 3;            // planos POR playfield (DPF 3+3 = 6 HW)
constexpr eng::u16 kDisplayH = 288;        // anillo = 256 + 2*16

// Mundo: 400 x 2048 px -> 25 x 128 tiles.
constexpr eng::u16 kMapCols = 25;
constexpr eng::u16 kMapRows = 128;
constexpr eng::u16 kTilesetCount = 128;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Generador de filas del tileset (128 tiles = 16 glifos x 8 variantes).
eng::u16 shooter_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return field::demo::pf_plane_row(glyph, static_cast<eng::u8>(variant & 3u), row, plane, 0, false);
}

// Paleta DPF de 16 registros: BG (PF1, regs 0..7) + objetos (PF2, regs 8..15).
constexpr eng::u16 kPalette[16] {
	// BG (fondo azul).
	0x000, 0x013, 0x025, 0x037, 0x049, 0x15b, 0x26d, 0x37f,
	// Objetos (naves/disparos: cian/blanco/rojo/verde).
	0x0cf, 0x7ef, 0xfff, 0xf40, 0x0f0, 0xfd0, 0xf0f, 0xaaa,
};

eng::u16 g_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::u8 patrol = 0;          // fase del vaivén X
	eng::u8 patrol_acc = 0;
	bool ready = false;

	// Objetos del juego (capa FG lienzo, regs 8..15). Nave fija + balas.
	struct Bullet { eng::s16 x = 0, y = 0, py = 0; bool live = false; };
	Bullet m_bullets[6] {};
	eng::s16 m_ship_x = 152;
	eng::u8 m_fire = 0;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011001u);
			return;
		}
		// Mapa: rejilla de tiles determinista (128 tiles).
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				g_map[static_cast<eng::u32>(y) * kMapCols + x] =
					static_cast<eng::u16>(field::demo::cell_hash(x, y, 0x5eedu) & (kTilesetCount - 1u));
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;                                  // anillo corkscrew (Y)
		scene_cfg.x_mode = field::AxisMode::Finite;                 // X lineal acotado
		scene_cfg.scroll_mode = field::ScrollMode::OneDirection;    // solo fila entrante
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		scene_cfg.map.wrap_x = 0;    // X finito: sin wrap
		scene_cfg.map.wrap_y = 0;    // Y acotado (se recorre de abajo arriba)
		scene_cfg.map.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &shooter_row;
		scene_cfg.bg_row_fn = &shooter_row;
		scene_cfg.palette = kPalette;
		// DPF: BG = corkscrew XYLimited (PF1, regs 0..7); FG = lienzo plano de
		// objetos (PF2, regs 8..15) que el juego dibuja cada frame.
		scene_cfg.dpf.enabled = true;
		scene_cfg.dpf.fg_canvas = true;
		scene_cfg.dpf.foreground_is_pf2 = true; // objetos (PF2) DELANTE del BG

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011002u);
			return;
		}
		// Arranca abajo del mundo, a media anchura (la nave sube).
		scene.bg().set_camera(kViewportW / 4, static_cast<eng::s32>(kMapRows * kTileH) - kViewportH);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011003u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011004u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x11000000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});

		// Vaivén X lento (0..80) y avance Y hacia arriba (-2 px/frame).
		if (++patrol_acc >= 4) { patrol_acc = 0; ++patrol; }
		const eng::s32 target_x = (patrol & 128u) ? 80 : 0;
		const eng::s32 cur_x = scene.bg().mapposx();
		const eng::s32 dx = (target_x > cur_x) ? (target_x - cur_x > 2 ? 2 : target_x - cur_x)
		                                        : (cur_x - target_x > 2 ? -2 : target_x - cur_x);
		const eng::s32 dy = -2;

		if (!scene.bg().update_scroll(plan, dx, dy)) {
			// Tope del mundo (arriba): reinicia abajo (demo infinita).
			scene.bg().set_camera(kViewportW / 4, static_cast<eng::s32>(kMapRows * kTileH) - kViewportH);
		}
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00011010u);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00011011u);
			return;
		}

		// --- FG: objetos (nave + balas) dibujados en el lienzo plano (PF2). -----
		// Dirty-rect: se borra lo anterior y se pinta lo nuevo (el color 0 de PF2
		// es transparente, deja ver el BG).
		const eng::s16 ship_y = 200;
		{
			auto fg = scene.canvas_fg_surface();
			// Borra nave y balas previas.
			fg.fill_rect(m_ship_x, ship_y, 16, 14, 0);
			for (auto& b : m_bullets) if (b.live) fg.fill_rect(b.x, b.py, 2, 6, 0);
			// Dispara cada 8 frames a una bala libre.
			if ((++m_fire & 7u) == 0u) {
				for (auto& b : m_bullets) {
					if (!b.live) { b.live = true; b.x = static_cast<eng::s16>(m_ship_x + 7); b.y = static_cast<eng::s16>(ship_y - 6); break; }
				}
			}
			// Avanza balas hacia arriba.
			for (auto& b : m_bullets) {
				if (!b.live) continue;
				b.y = static_cast<eng::s16>(b.y - 5);
				if (b.y < 2) b.live = false;
			}
			// Pinta nave (triángulo aproximado) y balas. Colores 1..7 = regs 8..15.
			fg.fill_rect(static_cast<eng::s16>(m_ship_x + 6), ship_y, 4, 8, 2);
			fg.fill_rect(static_cast<eng::s16>(m_ship_x + 2), static_cast<eng::s16>(ship_y + 8), 12, 4, 1);
			fg.fill_rect(m_ship_x, static_cast<eng::s16>(ship_y + 12), 16, 2, 3);
			for (auto& b : m_bullets) if (b.live) { fg.fill_rect(b.x, b.y, 2, 6, 7); b.py = b.y; }
		}

		// Telemetría: cámara X/Y para el assert de movimiento en regresión.
		g_eng_run_status.detail = 0x11000000u |
			((static_cast<eng::u32>(scene.bg().mapposx()) & 0x3ffu) << 10) |
			(static_cast<eng::u32>(scene.bg().mapposy()) & 0x3ffu);
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
