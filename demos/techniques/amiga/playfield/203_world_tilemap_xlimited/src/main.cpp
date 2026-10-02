// Lanzar desde PowerShell con Git Bash:
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh demos/techniques/amiga/playfield/203_world_tilemap_xlimited --debug --clean
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh demos/techniques/amiga/playfield/203_world_tilemap_xlimited --keep-running

// Demo 203: World::TileLayer → WorldTileMapView → XlimitedScene.

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/scene/world_tile_map.hpp>

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
namespace tilemap = eng::graphics::tilemap;

constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u16 kMapWidth = 256u;
constexpr eng::u16 kMapHeight = 20u;
constexpr eng::u8 kPlanes = 3u;
constexpr eng::u16 kTilesetCount = 128u;

eng::u16 g_tile_rows[16][4][kPlanes][kTile] {};
#if defined(ENG_203_DIRECT_TILEMAP_BENCH)
eng::u16 g_direct_cells[kMapWidth * kMapHeight] {};
#endif

eng::u16 tile_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return g_tile_rows[glyph & 15u][variant & 3u][plane][row & 15u];
}

constexpr field::ScrollConsts kScroll {
	kTile, kTile, kHeight, static_cast<eng::u32>(kHeight) * kPlanes, kPlanes};
#if defined(ENG_203_DIRECT_TILEMAP_BENCH)
using MapView = field::TileLayerMap;
#else
using MapView = eng::scene::WorldTileMapView;
#endif
using Profile = field::ScrollProgressive;

tilemap::PackedTileCell g_cells[kMapWidth * kMapHeight] {};

constexpr eng::u16 kPalette[32] {
	0x000, 0x131, 0x242, 0x353, 0x464, 0x575, 0x686, 0x797,
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
	0x000, 0xf80, 0xfd0, 0xff0, 0xf40, 0xf66, 0xfff, 0xaaa,
	0x421, 0x742, 0x085, 0x0aa, 0x4dd, 0xf6c, 0xf3a, 0x221,
};

/// Crea un relieve por bandas para que la cámara recorra formas amplias y legibles,
/// mientras cada celda sigue siendo un `PackedTileCell` del `TileMap16` de World.
void build_map() {
	for (eng::u16 y = 0u; y < kMapHeight; ++y) {
		for (eng::u16 x = 0u; x < kMapWidth; ++x) {
			eng::u16 tile = 0u;
			if (y < 7u) {
				tile = ((x + y * 5u) % 19u == 0u) ? 1u : 0u;
			} else if (y == 7u) {
				tile = static_cast<eng::u16>(2u + ((x / 4u) & 1u));
			} else if (y < 13u) {
				tile = static_cast<eng::u16>(4u + ((x / 3u + y) & 3u));
			} else {
				tile = static_cast<eng::u16>(8u + ((x / 2u + y * 3u) & 3u));
			}
			g_cells[static_cast<eng::u32>(y) * kMapWidth + x].set_tile(tile);
#if defined(ENG_203_DIRECT_TILEMAP_BENCH)
			g_direct_cells[static_cast<eng::u32>(y) * kMapWidth + x] = tile;
#endif
		}
	}
}

/// Precalcula las filas invariantes para que el callback del driver solo indexe el tileset.
void build_tile_rows() {
	for (eng::u8 glyph = 0u; glyph < 16u; ++glyph) {
		for (eng::u8 variant = 0u; variant < 4u; ++variant) {
			for (eng::u8 plane = 0u; plane < kPlanes; ++plane) {
				for (eng::u8 row = 0u; row < kTile; ++row) {
					g_tile_rows[glyph][variant][plane][row] =
						field::demo::pf_plane_row(glyph, variant, row, plane, 0u, false);
				}
			}
		}
	}
}

struct DemoGame {
	eng::scene::World<2u> world {};
	tilemap::TileMap16 tile_map {};
	eng::Ref<eng::scene::Layer> terrain {};
	field::XlimitedScene<kScroll, MapView, Profile> scene {};
	field::XlimitedSceneConfigT<MapView> config {};
	eng::graphics::FramePlan plan {};
	eng::s16 direction = 1;
	bool ready = false;

	/// Pinta la nave de referencia una vez sobre PF2 en init; no rasteriza píxeles en el bucle.
	void draw_ship() {
		auto fg = scene.canvas_fg_surface();
		constexpr eng::s16 x = 64;
		constexpr eng::s16 y = 112;
		fg.fill_rect(static_cast<eng::s16>(x + 23), static_cast<eng::s16>(y + 1), 7, 2, 7u);
		fg.fill_rect(static_cast<eng::s16>(x + 17), static_cast<eng::s16>(y + 3), 13, 2, 3u);
		fg.fill_rect(static_cast<eng::s16>(x + 11), static_cast<eng::s16>(y + 5), 20, 2, 3u);
		fg.fill_rect(static_cast<eng::s16>(x + 5), static_cast<eng::s16>(y + 7), 28, 2, 3u);
		fg.fill_rect(x, static_cast<eng::s16>(y + 9), 36, 4, 3u);
		fg.fill_rect(static_cast<eng::s16>(x + 4), static_cast<eng::s16>(y + 13), 30, 2, 3u);
		fg.fill_rect(static_cast<eng::s16>(x + 9), static_cast<eng::s16>(y + 15), 24, 2, 3u);
		fg.fill_rect(static_cast<eng::s16>(x + 15), static_cast<eng::s16>(y + 17), 17, 2, 3u);
		fg.fill_rect(static_cast<eng::s16>(x + 21), static_cast<eng::s16>(y + 19), 9, 2, 7u);
		fg.fill_rect(static_cast<eng::s16>(x + 21), static_cast<eng::s16>(y + 6), 8, 7, 1u);
	}

	/// Inicializa World, adapta su mapa retenido y crea la escena XLimited que lo consume.
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020301u);
			return;
		}
		build_tile_rows();
		build_map();
		tile_map = tilemap::TileMap16 {
			eng::Span<tilemap::PackedTileCell> {g_cells, kMapWidth * kMapHeight}, kMapWidth, kMapHeight};
		eng::scene::TileLayer content {};
		content.id = "terrain";
		content.map = tile_map;
		terrain = world.add_tile_layer("world-terrain", 0u, content);
		if (!terrain) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020302u);
			return;
		}
		terrain->camera().reset(
			eng::scene::WorldRect {0u, 0u, static_cast<eng::u16>(kMapWidth * kTile),
					       static_cast<eng::u16>(kMapHeight * kTile)},
			eng::Size2u {kWidth, kHeight});

		config.viewport_w = kWidth;
		config.viewport_h = kHeight;
		config.tile_width = kTile;
		config.tile_height = kTile;
		config.planes = kPlanes;
		config.y_mode = field::AxisPolicy::Off;
		config.direction = field::DirectionPolicy::Bidirectional;
		config.display_height = kHeight;
		config.max_step = 1u;
#if defined(ENG_203_DIRECT_TILEMAP_BENCH)
		// Variante A/B: TileLayerMap directo con los mismos índices, límites, wrap y driver.
		config.map.cells = eng::Span<const eng::u16> {g_direct_cells, kMapWidth * kMapHeight};
		config.map.width = kMapWidth;
		config.map.height = kMapHeight;
		config.map.wrap_x = kMapWidth;
		config.map.edge_tile = 0u;
		config.map.empty_tile = 0xffffu;
#else
		config.map = MapView {*terrain, kMapWidth, kMapHeight, 0u, 0xffffu};
#endif
		config.tileset_count = kTilesetCount;
		config.fg_row_fn = &tile_row;
		config.bg_row_fn = &tile_row;
		config.palette = kPalette;
		config.dpf.enabled = true;
		config.dpf.fg_canvas = true;
		config.dpf.foreground_is_pf2 = true;
		if (!scene.begin(backend.memory_manager(), config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020303u);
			return;
		}
		scene.bg().set_camera(0, 0);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020304u);
			return;
		}
		draw_ship();
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020304u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x20300000u);
	}

	/// Mueve la cámara retenida, dibuja un actor contrastado y pasa el delta al driver.
	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		terrain->camera().begin_frame();
		terrain->camera().move_by(direction, 0);
		if (terrain->camera().delta_x() == 0) {
			direction = static_cast<eng::s16>(-direction);
			terrain->camera().begin_frame();
			terrain->camera().move_by(direction, 0);
		}
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});
		if (!scene.update(plan, terrain->camera().delta_x(), terrain->camera().delta_y(),
				  context.frame.frame_index) || !backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020305u);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020305u);
			return;
		}
		const eng::u32 x = terrain->camera().scroll_x();
		g_eng_run_status.detail = 0x20300000u | ((x & 0xffu) << 16) | ((x >> 8) & 0xffu);
	}

	/// Instala la composición ya preparada y publica el frame actual al Copper.
	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

/// Punto de entrada Amiga: arranca el backend y deja que el engine conduzca frames hasta salir.
int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
