// Lanzar desde PowerShell con Git Bash:
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh games/500_caverna --release
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh games/500_caverna --config A500_release

// Demo 203: `World::TileLayer` → `WorldTileMapView` → `XlimitedScene`, **conducida por la fachada
// `App`** (F4 de `ROADMAP_GAME_API`): el juego declara el motor (config conocida en compilación —
// aquí un corcóscru DPF) y lo **registra** con `app.add_scroll_layer(scene)`; el `App` lo arranca y
// lo conduce por frame siguiendo la cámara del juego. El juego no llama a
// `update`/`compose`/`install` ni ve `FramePlan`. El motor elige el camino según su configuración
// (aquí la técnica del corcóscru); un nivel distinto puede declarar otro motor conocido a priori.

#include <eng/api/api.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/xlimited_scroll_layer.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga/input_poll.hpp>
#include <eng/scene/dpf_plan.hpp>
#include <eng/scene/plan.hpp>

namespace {

namespace playfield = eng::playfield;
namespace tilemap = eng::graphics::tilemap;

constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 208u;
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
using MapView = eng::playfield::FixedTileLayerMap<kMapWidth>;
using Profile = playfield::ScrollProgressive;

eng::u16 g_cells[kMapWidth * kMapHeight] {};

constexpr eng::u16 kPalette[32] {
	0x000, 0x131, 0x242, 0x353, 0x464, 0x575, 0x686, 0x797,
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
	0x000, 0xf80, 0xfd0, 0xff0, 0xf40, 0xf66, 0xfff, 0xaaa,
	0x421, 0x742, 0x085, 0x0aa, 0x4dd, 0xf6c, 0xf3a, 0x221,
};

/// Crea un relieve por bandas para que la cámara recorra formas amplias y legibles,
/// con el mapa fijo `FixedTileLayerMap<256>`.
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
			g_cells[static_cast<eng::u32>(y) * kMapWidth + x] = tile;
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
	playfield::XlimitedScene<kScroll, MapView, Profile> scene {};
	// Adapta la escena a la interfaz `ScrollLayer` que conduce el `App` (tipada, sin `void*`).
	playfield::XlimitedScrollLayer<playfield::XlimitedScene<kScroll, MapView, Profile>,
				       eng::amiga::AmigaBackend> layer {scene};
	playfield::XlimitedSceneConfigT<MapView> config {};
	// Cámara del motor (posición px que conduce el `App`); se sincroniza con la del `World`.
	eng::s32 cam_x = 0;
	eng::s32 cam_y = 0;
	eng::s16 player_x = 152;
	eng::s16 player_y = 96;
	static constexpr eng::u8 kEnemyCount = 4u;
	eng::s16 enemy_x[kEnemyCount] {32, 96, 168, 264};
	eng::s16 enemy_y[kEnemyCount] {128, 152, 176, 136};
	static constexpr eng::u8 kSpriteCount = 1u + kEnemyCount;
	static constexpr eng::u16 kSpriteBytes = 64u;
	eng::graphics::SpriteConfig m_sprites[kSpriteCount] {};
	eng::ChipView<eng::SpriteTag> m_sprite_data {};
	bool joy_left = false;
	bool joy_right = false;
	bool joy_up = false;
	bool joy_down = false;
	bool ready = false;

	/// DATA de sprite: 16 lineas de DAT/DATB por slot; el jugador y cuatro enemigos.
	void init_sprites() {
		m_sprite_data = scene.sprites().sprite_data_view();
		auto words = scene.sprites().sprite_words();
		words.fill(0u);
		for (eng::u8 slot = 0u; slot < kSpriteCount; ++slot) {
			eng::ChipView<eng::SpriteTag> view =
				m_sprite_data.subview(static_cast<eng::usize>(slot) * kSpriteBytes, kSpriteBytes);
			for (eng::u8 line = 2u; line < 14u; ++line) {
				words[static_cast<eng::usize>(slot) * 32u + static_cast<eng::usize>(line) * 2u] = 0x03c0u;
			}
			m_sprites[slot].enabled = true;
			m_sprites[slot].data = view;
			m_sprites[slot].width_words = 1u;
			m_sprites[slot].height = 16u;
			m_sprites[slot].attach = false;
		}
		m_sprites[0].hpos = static_cast<eng::u16>(0x81u + player_x);
		m_sprites[0].vstart = static_cast<eng::u16>(0x2cu + player_y);
		m_sprites[0].vstop = static_cast<eng::u16>(m_sprites[0].vstart + 16u);
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			const eng::u8 slot = static_cast<eng::u8>(i + 1u);
			m_sprites[slot].hpos = static_cast<eng::u16>(0x81u + enemy_x[i]);
			m_sprites[slot].vstart = static_cast<eng::u16>(0x2cu + enemy_y[i]);
			m_sprites[slot].vstop = static_cast<eng::u16>(m_sprites[slot].vstart + 16u);
			scene.sprites().set(slot, m_sprites[slot]);
		}
		scene.sprites().set(0u, m_sprites[0]);
		scene.invalidate_composition();
	}

	void place_player() {
		const eng::u16 hpos = static_cast<eng::u16>(0x81u + player_x);
		const eng::u16 vstart = static_cast<eng::u16>(0x2cu + player_y);
		if (m_sprites[0].hpos == hpos && m_sprites[0].vstart == vstart) return;
		m_sprites[0].hpos = hpos;
		m_sprites[0].vstart = vstart;
		m_sprites[0].vstop = static_cast<eng::u16>(vstart + 16u);
		scene.sprites().set(0u, m_sprites[0]);
		scene.invalidate_composition();
	}


	/// Inicializa World, adapta su mapa retenido, configura el motor y lo **registra** en el `App`.
	void init(auto& app) {
		eng::os::input_enable(eng::os::InputMouse | eng::os::InputJoystick);
		build_tile_rows();
		build_map();

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
		playfield::ScrollPlan fg = bg;
		eng::scene::ScenePlan<2u> plan {};
		(void)plan.add(eng::scene::LayerRole::Background, eng::scene::LayerPlacement {}, bg);
		(void)plan.add(eng::scene::LayerRole::Foreground,
			       eng::scene::LayerPlacement {0u, 0u, 2u}, fg);
		if (!eng::scene::apply_dpf_plan(config, plan)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		// Específico del corcóscru: política de ejes, mapa y generador de filas.
		config.y_mode = playfield::AxisPolicy::Off;
		config.direction = playfield::DirectionPolicy::Bidirectional;
		config.max_step = 2u;
		config.map.cells = eng::Span<const eng::u16> {g_cells, kMapWidth * kMapHeight};
		config.map.height = kMapHeight;
		config.map.wrap_x = 0u;
		config.map.wrap_y = 0u;
		config.map2.cells = eng::Span<const eng::u16> {g_cells, kMapWidth * kMapHeight};
		config.map2.height = kMapHeight;
		config.map2.wrap_x = 0u;
		config.map2.wrap_y = 0u;
		config.tileset_count = kTilesetCount;
		config.fg_row_fn = &tile_row;
		config.bg_row_fn = &tile_row;
		// Vuelca la config declarada a la escena y liga la cámara; el `App` arranca y conduce.
		config.sprite_data_bytes = kSpriteCount * kSpriteBytes;
		config.sprites_armed = true;
		scene.set_config(config);
		scene.track_camera(&cam_x, &cam_y);
		if (!app.add_scroll_layer(layer, eng::scene::LayerRole::Background,
					  eng::scene::LayerPlacement {})) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		init_sprites();
		ready = true;
		eng::amiga::mark_ready();
	}

	/// Movimiento de cámara y jugador; el `App` conduce el frame.
	void update(auto& app) {
		if (!ready) return;
		eng::s16 mouse_dx = 0;
		eng::s16 mouse_dy = 0;
		auto& port = eng::os::system_port();
		if (!port.empty()) {
			eng::os::Msg msg {};
			while (port.pop(msg)) {
				switch (msg.type) {
				case eng::os::MsgType::MouseMove:
					mouse_dx = static_cast<eng::s16>(mouse_dx + msg.payload.mouse.dx);
					mouse_dy = static_cast<eng::s16>(mouse_dy + msg.payload.mouse.dy);
					break;
				case eng::os::MsgType::Joystick:
					if (msg.payload.joy.port == 2u) {
						joy_left = (msg.payload.joy.dirs & eng::amiga::kJoyLeft) != 0u;
						joy_right = (msg.payload.joy.dirs & eng::amiga::kJoyRight) != 0u;
						joy_up = (msg.payload.joy.dirs & eng::amiga::kJoyUp) != 0u;
						joy_down = (msg.payload.joy.dirs & eng::amiga::kJoyDown) != 0u;
					}
					break;
				default:
					break;
				}
			}
		}
		const eng::s16 dx = static_cast<eng::s16>((joy_right ? 2 : 0) - (joy_left ? 2 : 0) + mouse_dx);
		const eng::s16 dy = static_cast<eng::s16>((joy_down ? 1 : 0) - (joy_up ? 1 : 0) + mouse_dy);
		if (dx == 0 && dy == 0) return;
		player_x = static_cast<eng::s16>(player_x + dx);
		player_y = static_cast<eng::s16>(player_y + dy);
		if (player_x < 8) player_x = 8;
		if (player_x > 296) player_x = 296;
		if (player_y < 8) player_y = 8;
		if (player_y > 184) player_y = 184;

		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			const eng::s16 ddx = static_cast<eng::s16>(player_x - enemy_x[i]);
			const eng::s16 ddy = static_cast<eng::s16>(player_y - enemy_y[i]);
			if (ddx > -16 && ddx < 16 && ddy > -16 && ddy < 16) {
				player_x = static_cast<eng::s16>(player_x - 2);
			}
		}

		cam_x += dx;
		if (cam_x < 0) cam_x = 0;
		if (cam_x > static_cast<eng::s32>(kMapWidth * kTile - kWidth)) {
			cam_x = static_cast<eng::s32>(kMapWidth * kTile - kWidth);
		}
		place_player();
	}

	/// El `App` materializa el mundo y conduce el motor; el juego solo publica telemetría.
	void render(auto& app) {
		if ((app.frame() & 7u) == 0u) {
			m_host_hud.draw(app.debug(), "CAVERNA DEMO 500", eng::debug::Version {1u, 0u},
					app.frame(), eng::debug::prof_clock());
		}
	}

	eng::debug::HostHud m_host_hud {};
};

} // namespace

int main() {
	eng::GameDisplay display {};
	display.width = kWidth;
	display.height = kHeight;
	display.color_depth = 1u;
	DemoGame game {};
	return eng::amiga::run_demo(game, display, eng::amiga::VblankWait::Polling);
}
