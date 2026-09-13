// ============================================================================
// Demo 112 - XYLimited 5 planos con fondo estilo RoboCod (parallax por plano)
// ============================================================================
//
// Un solo playfield de 5 planos:
//   - Planos 0..3: tilemap principal (16 colores, indices 0..15).
//   - Plano 4:    patron de fondo GEOMETRICO (tiles procedurales) con su propio
//                 scroll (`parallax_div=2`): su BPLxPT avanza a la mitad que el
//                 resto, asi el fondo se desliza mas despacio (RoboCod).
// La paleta son 32 indices pero MAPEADOS a 16 tonos: los indices 16..31 (plano 4
// activo) son los mismos tonos oscurecidos; alli donde el tilemap es transparente
// (indice 0) el fondo aparece en color propio (indice 16).
//
//   X: Ring (XLimited)   Y: scroll_y=false (X-only)
//   Plan 400x256 px, tiles 16, mapa 256x16 (toroidal en X).

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
constexpr eng::u8  kPlanes = 5;            // 4 principal + 1 fondo (parallax)
constexpr eng::u8  kParallaxPlane = 4;
constexpr eng::u16 kDisplayH = 288;        // corkscrew: 256 + 2*16

constexpr eng::u16 kMapCols = 25;          // 400 px de ancho (X Finite)
constexpr eng::u16 kMapRows = 32;          // 512 px de alto (mas que la ventana)
constexpr eng::u16 kTilesetCount = 128;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Tilemap principal RALO: solo una cuadricula clara (borde de tile) + un marcador
// de variante. El resto transparente -> el patron de fondo (plano 4) se ve claro.
eng::u16 robocod_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	if (plane >= 4u) return 0u;
	const bool border = (row == 0u) || (row == 15u);
	const bool marker = (row >= 6u && row <= 9u) && (((row - 6u) >> 1u) <= (variant & 1u));
	const eng::u16 mask = border ? 0xffffu : (marker ? 0x0ff0u : 0x8001u);
	const eng::u8 c = 7u; // blanco/azul claro (indice 7)
	return (c & (1u << plane)) != 0u ? mask : 0u;
}

// Paleta de 32 indices mapeada a 16 tonos. Los indices 16..31 (plano de fondo
// activo) son los mismos tonos AZULADOS mas oscuros (preservando el tono), salvo
// el 16 que es el color propio del fondo (donde el tilemap es transparente).
constexpr eng::u16 kPalette[32] {
	// 0..15 principal.
	0x000, 0x123, 0x235, 0x347, 0x459, 0x56b, 0x67d, 0x9bf,
	0x9af, 0x0cf, 0x0f9, 0xff0, 0xf80, 0xf40, 0xf00, 0xfff,
	// 16..31: bit 4 (fondo). 16 = color del fondo (azul medio). 17..31 = version
	// oscura del MISMO tono (el grid usa 7 -> 23 debe ser azul oscuro, no teal).
	0x257, 0x012, 0x023, 0x034, 0x045, 0x046, 0x057, 0x458,
	0x079, 0x00a, 0x0a7, 0x880, 0x840, 0x820, 0x800, 0x888,
};

eng::u16 g_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::s16 m_dx = 1;
	eng::s16 m_dy = 1;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011201u);
			return;
		}
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				g_map[static_cast<eng::u32>(y) * kMapCols + x] =
					static_cast<eng::u16>(field::demo::cell_hash(x, y, 0x0b0b0u) & (kTilesetCount - 1u));
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;                                  // corkscrew (movimiento Y real)
		scene_cfg.x_mode = eng::field::AxisMode::Finite;            // X lineal acotado (sin unroll)
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;
		scene_cfg.parallax_plane = kParallaxPlane;   // fondo RoboCod
		scene_cfg.parallax_div = 2;

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		scene_cfg.map.wrap_x = 0;      // X finito (400 px)
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &robocod_row;
		scene_cfg.bg_row_fn = &robocod_row;
		scene_cfg.palette = kPalette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011202u);
			return;
		}
		scene.bg().set_camera(0, 0);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011203u);
			return;
		}
		// El fill del tilemap escribe 0 en el plano 4: re-pintar el patron DESPUES.
		scene.bg().fill_parallax_pattern();
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011204u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x11200000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});
		// Diagonal con rebote: X en [0, 80] (area extra de 400-320) e Y en
		// [0, kMapRows*16 - 256] (area extra vertical de 512-256).
		const eng::s32 camx = scene.bg().mapposx();
		const eng::s32 camy = scene.bg().mapposy();
		if (camx >= static_cast<eng::s32>(kMapCols * kTileW) - kViewportW) m_dx = -1;
		if (camx <= 0) m_dx = 1;
		if (camy >= static_cast<eng::s32>(kMapRows * kTileH) - kViewportH) m_dy = -1;
		if (camy <= 0) m_dy = 1;
		const eng::s32 dx = (m_dx > 0) ? 2 : -2;
		const eng::s32 dy = (m_dy > 0) ? 1 : -1;
		if (!scene.bg().update_scroll(plan, dx, dy)) {
			m_dx = -m_dx; m_dy = -m_dy;
		}
		if (!backend.execute_frame_plan(plan)) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011210u); return;
		}
		if (!scene.compose()) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011211u); return;
		}
		g_eng_run_status.detail = 0x11200000u |
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
