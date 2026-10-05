// Lanzar desde PowerShell con Git Bash:
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh demos/techniques/amiga/playfield/203_world_tilemap_xlimited --debug --clean
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh demos/techniques/amiga/playfield/203_world_tilemap_xlimited --keep-running

// Demo 203: `World::TileLayer` → `WorldTileMapView` → `XlimitedScene`, **conducida por la fachada
// `App`** (F4 de `ROADMAP_GAME_API`): el juego declara el motor (config conocida en compilación —
// aquí un corcóscru DPF) y lo **registra** con `app.add_scroll_layer(scene)`; el `App` lo arranca y
// lo conduce por frame siguiendo la cámara del juego. El juego no llama a
// `update`/`compose`/`install` ni ve `FramePlan`. El motor elige el camino según su configuración
// (aquí la técnica del corcóscru); un nivel distinto puede declarar otro motor conocido a priori.

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/xlimited_scroll_layer.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/scene/dpf_plan.hpp>
#include <eng/scene/plan.hpp>
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

namespace playfield = eng::playfield;
namespace tilemap = eng::graphics::tilemap;

constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 256u;
constexpr eng::u16 kMapWidth = 256u;
constexpr eng::u16 kMapHeight = 20u;
constexpr eng::u8 kPlanes = 3u;
constexpr eng::u16 kTilesetCount = 128u;

eng::u16 g_tile_rows[16][4][kPlanes][kTile] {};

eng::u16 tile_row(eng::u8 glyph, eng::u8 variant, eng::u8 row, eng::u8 plane) {
	return g_tile_rows[glyph & 15u][variant & 3u][plane][row & 15u];
}

constexpr playfield::ScrollConsts kScroll {
	kTile, kTile, kHeight, static_cast<eng::u32>(kHeight) * kPlanes, kPlanes};
using MapView = eng::scene::WorldTileMapView;
using Profile = playfield::ScrollProgressive;

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
						playfield::demo::pf_plane_row(glyph, variant, row, plane, 0u, false);
				}
			}
		}
	}
}

struct DemoGame {
	eng::scene::World<2u> world {};
	tilemap::TileMap16 tile_map {};
	eng::Ref<eng::scene::Layer> terrain {};
	playfield::XlimitedScene<kScroll, MapView, Profile> scene {};
	// Adapta la escena a la interfaz `ScrollLayer` que conduce el `App` (tipada, sin `void*`).
	playfield::XlimitedScrollLayer<playfield::XlimitedScene<kScroll, MapView, Profile>,
				       eng::amiga::AmigaBackend> layer {scene};
	playfield::XlimitedSceneConfigT<MapView> config {};
	// Cámara del motor (posición px que conduce el `App`); se sincroniza con la del `World`.
	eng::s32 cam_x = 0;
	eng::s32 cam_y = 0;
	eng::s16 direction = 1;
	bool ready = false;

	/// Pinta la nave de referencia una vez sobre PF2 tras arrancar la escena; no rasteriza en el bucle.
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

	/// Inicializa World, adapta su mapa retenido, configura el motor y lo **registra** en el `App`.
	void init(auto& app) {
		eng::debug::mark_init_started(g_eng_run_status);
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

		// **Plan de escena** (vocabulario del planner, §7): un campo de scroll (BG) + un **lienzo**
		// FG (rol `Foreground`, `content = Canvas`). `apply_dpf_plan` siembra geometría/paleta y los
		// roles del DPF (`fg_canvas` + FG delante); lo específico del corcóscru va aparte.
		playfield::ScrollPlan bg {};
		bg.viewport_w = kWidth;
		bg.viewport_h = kHeight;
		bg.tile_w = kTile;
		bg.tile_h = kTile;
		bg.planes = kPlanes;
		bg.display_height = kHeight;
		bg.tilemap.palette = eng::PaletteWords {kPalette, 32u};
		eng::scene::ScenePlan<2u> plan {};
		(void)plan.add(eng::scene::LayerRole::Background, eng::scene::LayerPlacement {}, bg);
		(void)plan.add(eng::scene::LayerRole::Foreground,
			       eng::scene::LayerPlacement {0u, 0u, 2u}, {}, // FG en PF2 (delante)
			       eng::scene::LayerContent::Canvas);
		if (!eng::scene::apply_dpf_plan(config, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020307u);
			return;
		}
		// Específico del corcóscru: política de ejes, mapa y generador de filas.
		config.y_mode = playfield::AxisPolicy::Off;
		config.direction = playfield::DirectionPolicy::Bidirectional;
		config.max_step = 1u;
		config.map = MapView {*terrain, kMapWidth, kMapHeight, 0u, 0xffffu};
		config.tileset_count = kTilesetCount;
		config.fg_row_fn = &tile_row;
		config.bg_row_fn = &tile_row;
		// Vuelca la config declarada a la escena y liga la cámara; el `App` arranca y conduce.
		scene.set_config(config);
		scene.track_camera(&cam_x, &cam_y);
		if (!app.add_scroll_layer(layer, eng::scene::LayerRole::Background,
					  eng::scene::LayerPlacement {})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020303u);
			return;
		}
		draw_ship(); // el lienzo del FG ya existe tras `add_scroll_layer`
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x20300000u);
	}

	/// Mueve la cámara del `World` y sincroniza la del motor; el `App` conduce el frame.
	void update(auto& app) {
		eng::debug::mark_frame(g_eng_run_status, app.frame());
		if (!ready) return;
		terrain->camera().begin_frame();
		terrain->camera().move_by(direction, 0);
		if (terrain->camera().delta_x() == 0) {
			direction = static_cast<eng::s16>(-direction);
			terrain->camera().begin_frame();
			terrain->camera().move_by(direction, 0);
		}
		cam_x = static_cast<eng::s32>(terrain->camera().scroll_x());
		cam_y = static_cast<eng::s32>(terrain->camera().scroll_y());
		const eng::u32 x = static_cast<eng::u32>(cam_x);
		g_eng_run_status.detail = 0x20300000u | ((x & 0xffu) << 16) | ((x >> 8) & 0xffu);
	}

	/// El `App` materializa el mundo y conduce el motor; el juego solo publica telemetría.
	void render(auto& app) {
		eng::debug::probe_when_ready(g_eng_run_status, app.frame());
	}
};

} // namespace

/// Punto de entrada Amiga: arranca el backend, compone el display y deja que el `App` conduzca.
int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020301u);
		return 0;
	}
	// Display base mínimo: lo sobreescribe la escena corcóscru (su compositor hace el takeover).
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 1u;
	DemoGame game {};
	eng::App app {backend, game, backend.memory_manager()};
	if (!app.set_display(display) || !app.start()) {
		eng::debug::mark_failed(g_eng_run_status, 0x00020306u);
		return 0;
	}
	app.run(0xffffu);
	return 0;
}
