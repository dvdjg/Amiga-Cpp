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
#include <eng/scene/sprite_bank.hpp>

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

constexpr eng::u16 kSolidFirst = 2u;
constexpr eng::u16 kSolidLast = 11u;
constexpr eng::s16 kGravity = 1;
constexpr eng::s16 kJump = -8;
constexpr eng::s16 kPlayerSize = 16;

/// `true` si la celda es parte del terreno que bloquea al jugador. Los tiles
/// 0/1 son decoracion y los 2..11 son las plataformas/suelo del tileset.
bool tile_is_solid(eng::u16 tx, eng::u16 ty) {
	if (tx >= kMapWidth || ty >= kMapHeight) return true;
	const eng::u16 tile = g_cells[static_cast<eng::u32>(ty) * kMapWidth + tx];
	return tile >= kSolidFirst && tile <= kSolidLast;
}

/// Colision de la caja 16x16 con el tilemap en coordenadas de mundo. Se usa
/// `>> 4` (tile 16 px) y no division en el bucle de juego.
bool box_hits_solid(eng::s32 x, eng::s32 y) {
	if (x < 0 || y < 0) return true;
	const eng::s32 tx0 = x >> 4;
	const eng::s32 ty0 = y >> 4;
	const eng::s32 tx1 = (x + kPlayerSize - 1) >> 4;
	const eng::s32 ty1 = (y + kPlayerSize - 1) >> 4;
	for (eng::s32 ty = ty0; ty <= ty1; ++ty) {
		for (eng::s32 tx = tx0; tx <= tx1; ++tx) {
			if (tile_is_solid(static_cast<eng::u16>(tx), static_cast<eng::u16>(ty))) {
				return true;
			}
		}
	}
	return false;
}

/// Crea un relieve por bandas y una linea de pilares bajos para que el jugador
/// tenga obstaculos horizontales que superar saltando. El suelo esta en la fila
/// 7 (y=112) y los tiles 8..11 rellenan el terreno inferior.
void build_map() {
	for (eng::u16 y = 0u; y < kMapHeight; ++y) {
		for (eng::u16 x = 0u; x < kMapWidth; ++x) {
			eng::u16 tile = 0u;
			if (y == 6u && (x & 31u) == 0u) {
				tile = static_cast<eng::u16>(2u + ((x >> 4u) & 1u));
			} else if (y < 7u) {
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

constexpr eng::u8 kActorCount = 8u;

struct CavernaGame : eng::scene::SpriteBank<kActorCount> {
	playfield::XlimitedScene<kScroll, MapView, Profile> scene {};
	// Adapta la escena a la interfaz `ScrollLayer` que conduce el `App` (tipada, sin `void*`).
	playfield::XlimitedScrollLayer<playfield::XlimitedScene<kScroll, MapView, Profile>,
				       eng::amiga::AmigaBackend> layer {scene};
	playfield::XlimitedSceneConfigT<MapView> config {};
	// Cámara del motor (posición px que conduce el `App`); se sincroniza con la del `World`.
	eng::s32 cam_x = 0;
	eng::s32 cam_y = 0;
	eng::s16 player_wx = 152; // posicion de mundo, no de pantalla
	eng::s16 player_y = 96;
	eng::s16 player_vy = 0;
	bool on_ground = true;
	bool jump_prev = false;
	static constexpr eng::u8 kEnemyCount = 7u;
	eng::s16 enemy_x[kEnemyCount] {32, 96, 168, 264, 320, 384, 448};
	eng::s16 enemy_y[kEnemyCount] {80, 64, 88, 72, 56, 96, 64};
	eng::s8 enemy_dx[kEnemyCount] {1, -1, 1, -1, 1, -1, 1};
	static constexpr eng::u8 kSpriteCount = kActorCount;
	static constexpr eng::u16 kSpriteBytes = 64u;
	using Director = eng::scene::SpriteDirector<static_cast<eng::u16>(kActorCount),
					    static_cast<eng::u16>(kActorCount), 1u, 1u, 16u, 4u>;
	Director m_dir {};
	eng::scene::SpriteId m_actor_ids[kSpriteCount] {};
	eng::Span<eng::u16> m_sprite_words {};
	eng::s16 m_player_sx = -1;
	eng::s16 m_player_sy = -1;
	bool m_sprites_dirty = false;
	bool m_sprites_initialized = false;
	bool joy_left = false;
	bool joy_right = false;
	bool joy_up = false;
	bool joy_down = false;
	eng::u8 joy_fire = 0u;
	eng::u8 mouse_buttons = 0u;
	bool ready = false;

	[[nodiscard]] eng::s16 player_screen_x() const {
		const eng::s32 sx = static_cast<eng::s32>(player_wx) - cam_x;
		if (sx < 0) return 0;
		if (sx > static_cast<eng::s32>(kWidth - kPlayerSize)) return static_cast<eng::s16>(kWidth - kPlayerSize);
		return static_cast<eng::s16>(sx);
	}

	[[nodiscard]] eng::s16 enemy_screen_x(eng::u8 i) const {
		return static_cast<eng::s16>(enemy_x[i] - cam_x);
	}

	/// DATA de sprite: 16 lineas de DAT/DATB por slot; jugador y siete enemigos.
		void publish_sprites() {
		if (!m_sprites_dirty) return;
		if (!m_sprites_initialized) {
			(void)scene.emit_sprite_director(m_dir);
			m_sprites_initialized = true;
		} else if (!scene.refresh_sprite_director(m_dir)) {
			(void)scene.emit_sprite_director(m_dir);
		}
		m_sprites_dirty = false;
	}

	void init_sprites() {
		m_sprite_data = scene.sprites().sprite_data_view();
		auto words = scene.sprites().sprite_words();
		m_sprite_words = eng::Span<eng::u16> {words.data(), words.size()};
		m_sprite_words.fill(0u);
		for (eng::u8 slot = 0u; slot < kSpriteCount; ++slot) {
			for (eng::u8 line = 2u; line < 14u; ++line) {
				m_sprite_words[static_cast<eng::usize>(slot) * 32u +
						static_cast<eng::usize>(line) * 2u] = 0x03c0u;
			}
		}
		animate_enemies(0u);
		for (eng::u8 slot = 0u; slot < kSpriteCount; ++slot) {
			eng::graphics::Visual visual {};
			visual.pixels = eng::Span<const eng::u16> {
				m_sprite_words.data() + static_cast<eng::usize>(slot) * 32u, 32u};
			visual.w = 16u;
			visual.h = 16u;
			visual.bitplanes = 2u;
			const eng::scene::VisualId visual_id = m_dir.add_visual(visual);
			eng::scene::SpriteDesc desc {};
			desc.content = eng::scene::SpriteVisualRef::simple(visual_id);
			desc.hint.policy = eng::scene::SpritePolicy::Sprite;
			desc.hint.channel = slot;
			desc.x = slot == 0u ? player_screen_x() : enemy_screen_x(static_cast<eng::u8>(slot - 1u));
			desc.y = slot == 0u ? player_y : enemy_y[slot - 1u];
			m_actor_ids[slot] = m_dir.add(desc);
			if (!m_actor_ids[slot].valid()) {
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
				return;
			}
		}
		m_sprites_dirty = true;
		publish_sprites();
	}

	/// Mantiene al jugador en una posicion estable de pantalla y desplaza el
	/// mundo: la camara no se mueve a la vez que el sprite, sino que lo sigue.
	void follow_camera() {
		constexpr eng::s32 kScreenAnchor = 152;
		eng::s32 target = static_cast<eng::s32>(player_wx) - kScreenAnchor;
		if (target < 0) target = 0;
		const eng::s32 max_x = static_cast<eng::s32>(kMapWidth * kTile) - kWidth;
		if (target > max_x) target = max_x;
		cam_x = target;
		cam_y = 0;
	}

		void place_player() {
		const eng::s16 sx = player_screen_x();
		if (sx == m_player_sx && player_y == m_player_sy) return;
		if (!m_dir.set_position(m_actor_ids[0], sx, player_y)) return;
		m_player_sx = sx;
		m_player_sy = player_y;
		m_sprites_dirty = true;
	}
	/// Alterna el ancho del bloque de los enemigos cada 16 frames. Se escribe
	/// solo el DATA que el DMA relee; no hace falta reemitir la copperlist.
	void animate_enemies(eng::u32 frame) {
		if ((frame & 15u) != 0u) return;
		auto words = scene.sprites().sprite_words();
		const eng::u16 pattern = ((frame >> 4u) & 1u) != 0u ? 0x07e0u : 0x03c0u;
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			const eng::u8 slot = static_cast<eng::u8>(i + 1u);
			for (eng::u8 line = 2u; line < 14u; ++line) {
				words[static_cast<eng::usize>(slot) * 32u + static_cast<eng::usize>(line) * 2u] = pattern;
			}
		}
	}

	/// Patrulla los enemigos cada 8 frames y refresca sus sprites una sola vez.
	/// El refresco incluye la camara ya actualizada, para que no se queden fijos
	/// en pantalla mientras el mundo se desplaza.
		void update_enemies(eng::u32 frame) {
		if ((frame & 15u) != 0u) return;
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			enemy_x[i] = static_cast<eng::s16>(enemy_x[i] + enemy_dx[i] * 2);
			if (enemy_x[i] < 16) {
				enemy_x[i] = 16;
				enemy_dx[i] = 1;
			}
			if (enemy_x[i] > static_cast<eng::s16>(kMapWidth * kTile - 16u)) {
				enemy_x[i] = static_cast<eng::s16>(kMapWidth * kTile - 16u);
				enemy_dx[i] = -1;
			}
			(void)m_dir.set_position(m_actor_ids[static_cast<eng::u8>(i + 1u)],
						 enemy_screen_x(i), enemy_y[i]);
		}
		m_sprites_dirty = true;
	}
	/// Resuelve el contacto jugador/enemigo con una reaccion minima: invierte
	/// la patrulla y deja al jugador en la posicion horizontal previa.
	void resolve_enemy_hits(eng::s16 old_wx) {
		const eng::Box player { player_wx, player_y, kPlayerSize, kPlayerSize };
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			const eng::Box enemy { enemy_x[i], enemy_y[i], kPlayerSize, kPlayerSize };
			if (eng::overlaps(player, enemy)) {
				enemy_dx[i] = static_cast<eng::s8>(-enemy_dx[i]);
				player_wx = old_wx;
			}
		}
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
#ifdef CAVERNA_SINGLE
		// Variante de validacion del compositor single (sin DPF).
		config.dpf.enabled = false;
#endif
		// Específico del corcóscru: política de ejes, mapa y generador de filas.
		config.y_mode = playfield::AxisPolicy::Off;
		config.direction = playfield::DirectionPolicy::Bidirectional;
		config.max_step = 2u;
		config.map.cells = eng::Span<const eng::u16> {g_cells, kMapWidth * kMapHeight};
		config.map.height = kMapHeight;
		config.map.wrap_x = 0u;
		config.map.wrap_y = 0u;
		config.map.empty_tile = 0u; // el tile 0 es cielo/vacio, no se pinta
		config.map2.cells = eng::Span<const eng::u16> {g_cells, kMapWidth * kMapHeight};
		config.map2.height = kMapHeight;
		config.map2.wrap_x = 0u;
		config.map2.wrap_y = 0u;
		config.map2.empty_tile = 0u;
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

	/// Lee entrada por mensajes, integra gravedad/salto y resuelve la colision
	/// contra el tilemap antes de mover la camara y refrescar los sprites.
	void update(auto& app) {
		if (!ready) return;
		eng::s16 mouse_dx = 0;
		auto& port = eng::os::system_port();
		if (!port.empty()) {
			eng::os::Msg msg {};
			while (port.pop(msg)) {
				switch (msg.type) {
				case eng::os::MsgType::MouseMove:
					mouse_dx = static_cast<eng::s16>(mouse_dx + msg.payload.mouse.dx);
					break;
				case eng::os::MsgType::MouseButton:
					mouse_buttons = msg.payload.mouse.buttons;
					break;
				case eng::os::MsgType::Joystick:
					if (msg.payload.joy.port == 2u) {
						joy_left = (msg.payload.joy.dirs & eng::amiga::kJoyLeft) != 0u;
						joy_right = (msg.payload.joy.dirs & eng::amiga::kJoyRight) != 0u;
						joy_up = (msg.payload.joy.dirs & eng::amiga::kJoyUp) != 0u;
						joy_down = (msg.payload.joy.dirs & eng::amiga::kJoyDown) != 0u;
						joy_fire = msg.payload.joy.fire;
					}
					break;
				default:
					break;
				}
			}
		}
		const eng::s16 dx = static_cast<eng::s16>((joy_right ? 2 : 0) - (joy_left ? 2 : 0) + mouse_dx);
		const bool jump_down = (joy_fire != 0u) || ((mouse_buttons & 1u) != 0u) || joy_up;
		if (jump_down && !jump_prev && on_ground) {
			player_vy = kJump;
			on_ground = false;
		}
		jump_prev = jump_down;

		const eng::s16 old_wx = player_wx;
		if (dx != 0) {
			player_wx = static_cast<eng::s16>(player_wx + dx);
			if (player_wx < 8) player_wx = 8;
			if (player_wx > static_cast<eng::s16>(kMapWidth * kTile - 8u)) {
				player_wx = static_cast<eng::s16>(kMapWidth * kTile - 8u);
			}
			if (box_hits_solid(player_wx, player_y)) {
				player_wx = old_wx;
			}
		}

		player_vy = static_cast<eng::s16>(player_vy + kGravity);
		if (player_vy > 8) player_vy = 8;
		player_y = static_cast<eng::s16>(player_y + player_vy);
		if (player_y < 0) {
			player_y = 0;
			player_vy = 0;
		}
		const eng::s16 max_y = static_cast<eng::s16>(kHeight - kPlayerSize);
		if (player_y > max_y) {
			player_y = max_y;
			player_vy = 0;
		}
		if (box_hits_solid(player_wx, player_y)) {
			if (player_vy > 0) {
				while (player_y > 0 && box_hits_solid(player_wx, player_y)) {
					player_y = static_cast<eng::s16>(player_y - 1);
				}
				on_ground = true;
			} else if (player_vy < 0) {
				while (player_y < max_y && box_hits_solid(player_wx, player_y)) {
					player_y = static_cast<eng::s16>(player_y + 1);
				}
			}
			player_vy = 0;
		} else {
			on_ground = box_hits_solid(player_wx, player_y + 1);
		}

		resolve_enemy_hits(old_wx);
		follow_camera();
		place_player();
		update_enemies(app.frame());
		animate_enemies(app.frame());
		publish_sprites();
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
	CavernaGame game {};
	return eng::amiga::run_demo(game, display, eng::amiga::VblankWait::Polling);
}
