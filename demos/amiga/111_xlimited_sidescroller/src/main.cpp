// ============================================================================
// Demo 111 - Side-scroller horizontal (limite del scroll X) sobre XYLimited
// ============================================================================
//
// Escenario: mundo 4096 px de ancho x 320 px de alto, tiles 16x16 -> 256x20
// celdas, tileset de 128 tiles. Es un X-limited: la camara avanza en X (scroll
// largo, anillo XLimited) y el Y es CORTO (aqui fijo, `y_mode = Off`, mundo de
// 320 px con ventana de 256 -> 64 px de recorrido vertical potencial). Coste de
// Blitter proporcional al salto; framebuffer acotado.
//
//   X: AxisPolicy::Ring   (XLimited, anillo con banda entrante)
//   Y: y_mode = Off       (sin corkscrew; el Y del mundo cabe casi entero)
//
// DPF: BG = tilemap XLimited (PF1) + FG = lienzo plano de objetos (PF2, delante).

#include <eng/api/api.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/streaming_map.hpp>
#include <eng/field/tile_source.hpp>
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
constexpr eng::u16 kDisplayH = 256;        // y_mode=Off -> display = viewport

// Mundo: 4096 x 320 px -> 256 x 20 tiles.
constexpr eng::u16 kMapCols = 256;
constexpr eng::u16 kMapRows = 20;
constexpr eng::u16 kTilesetCount = 128;

// El scroll consume un ACCESOR de tiles, no la matriz: aquí el mundo vive en un
// `StreamingWorldMap` (chunks residentes en un pool del llamador) y el playfield
// solo ve una `TileMapView` (límites/wrap + `tile_at`). El `prefetch` de cada
// frame mantiene residentes los chunks que cubren la banda entrante.
constexpr eng::u16 kChunkTiles = 16;
constexpr eng::u8  kChunkCapacity = 12;
/// Fuente de chunks del mundo (concept `ChunkSource`): envuelve `load_chunk` para
/// que `StreamingWorldMap` la conozca en compilación (sin punteros a función).
struct WorldChunkSource {
	field::LoadResult load(eng::s32 cx, eng::s32 cy, eng::TileBankBuffer cells) const;
};
using WorldMap = field::StreamingWorldMap<kChunkTiles, kChunkCapacity, WorldChunkSource>;
using MapView = field::TileMapView<WorldMap>;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Selección ESTÁTICA del perfil de scroll (ver engine/include/eng/field/scroll_profile.hpp
// y docs/engine/architecture/FAST_SCROLL.md). El desarrollador cambia el comportamiento
// editando esta única línea: `ScrollProgressive` (2 px/frame, clásico), `ScrollFast1`
// (16 px/frame), `ScrollFast2` (32 px/frame), `ScrollFast4` (64 px/frame).
using ScrollProfile_t = field::ScrollProgressive;
// Paso de cámara por frame: el perfil rápido lo fija a N tiles; el progresivo conserva 2 px.
constexpr eng::s32 kStepX = ScrollProfile_t::fill_tiles
	? static_cast<eng::s32>(ScrollProfile_t::fill_tiles) * kTileW : 2;

eng::u16 side_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return field::demo::pf_plane_row(glyph, static_cast<eng::u8>(variant & 3u), row, plane, 0, false);
}

// Paleta DPF de 16: BG (PF1, 0..7) + objetos (PF2, 8..15).
constexpr eng::u16 kPalette[16] {
	0x000, 0x131, 0x242, 0x353, 0x464, 0x575, 0x686, 0x797,
	0x0cf, 0x7ef, 0xfff, 0xf40, 0x0f0, 0xfd0, 0xf0f, 0xaaa,
};

eng::u16 g_map[kMapCols * kMapRows] {};

// Pool de chunks residentes (del llamador). Son índices de tile (no DMA), así que
// basta con memoria estática.
eng::u16 g_world_pool[WorldMap::kPoolCells] {};

constexpr eng::s32 kChunkCols = kMapCols / kChunkTiles; // 16 chunks en X

// Carga el chunk `(cx,cy)`: rellena `kChunkTiles*kChunkTiles` celdas desde `g_map`.
// El wrap de X es a nivel de chunks (potencia de dos -> máscara). Las filas fuera
// del mundo se dejan a 0 (nunca se consultan: `wrap_y=0`).
field::LoadResult load_chunk(void*, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer cells) {
	const eng::s32 ccx = cx & (kChunkCols - 1);
	for (eng::u16 ly = 0; ly < kChunkTiles; ++ly) {
		const eng::s32 wy = cy * kChunkTiles + ly;
		for (eng::u16 lx = 0; lx < kChunkTiles; ++lx) {
			const eng::s32 wx = ccx * kChunkTiles + lx;
			cells[static_cast<eng::u32>(ly) * kChunkTiles + lx] =
				(wy >= 0 && wy < kMapRows)
					? g_map[static_cast<eng::u32>(wy) * kMapCols + static_cast<eng::u32>(wx)]
					: 0;
		}
	}
	return field::LoadResult::Ready;
}

field::LoadResult WorldChunkSource::load(eng::s32 cx, eng::s32 cy,
                                         eng::TileBankBuffer cells) const {
	return load_chunk(nullptr, cx, cy, cells);
}

struct DemoGame {
	field::XlimitedScene<kScrollConsts, MapView, ScrollProfile_t> scene {};
	field::XlimitedSceneConfigT<MapView> scene_cfg {};
	WorldMap m_world {};
	eng::graphics::FramePlan plan {};
	eng::s16 m_ship_y = 200;
	eng::s16 m_ship_py = 200;
	struct Bullet { eng::s16 x = 0, y = 0, px = 0; bool live = false; };
	Bullet m_bullets[6] {};
	eng::u8 m_fire = 0;
	bool ready = false;

	// Precarga los chunks que cubren la banda visible + margen de avance. El
	// scroll solo consulta residentes (`TileMapView::tile_at`), así que sin esto
	// aparecerían huecos al entrar en un chunk aún no cargado.
	void prefetch_band() {
		const eng::s32 tx0 = (scene.bg().mapposx() / kTileW) - 2;
		m_world.prefetch(tx0, 0, tx0 + (kViewportW / kTileW) + 20, kMapRows);
	}

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
		scene_cfg.y_mode = eng::field::AxisPolicy::Off;                                 // X-limited (Y fijo)
		scene_cfg.direction = eng::field::DirectionPolicy::Bidirectional;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;

		if (!m_world.init(WorldChunkSource {},
		                  eng::TileBankBuffer{g_world_pool}, 0xFFFFu)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011105u);
			return;
		}
		scene_cfg.map.src = &m_world;
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
		prefetch_band();
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

		// Avance X hacia la derecha (paso del perfil; por defecto 2 px/frame). El mapa
		// es toroidal, no se topea.
		prefetch_band();
		if (!scene.bg().update_scroll(plan, kStepX, 0)) {
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
