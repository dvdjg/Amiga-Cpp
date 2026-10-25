// Lanzar desde PowerShell con Git Bash:
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/build/build-demo.sh games/500_caverna --release
//   & 'C:\Program Files\Git\bin\bash.exe' ./tools/run/run-demo.sh games/500_caverna --config A500_release

// 500 Caverna: primer consumidor jugable de la fachada de juego. El juego declara
// un motor de scroll conocido (`XlimitedScene`, corcóscru DPF 3+3) y lo registra
// con `app.add_scroll_layer`; el `App` lo arranca y lo conduce por frame. La lógica
// no ve `FramePlan`, planos, Copper ni el backend.
//
// El arte es procedural y vive en `caverna_art.cpp`: este fichero solo contiene la
// lógica de juego (entrada, física, cámara, actores y HUD), que es lo que un lector
// debe poder copiar a su propio juego. La entrada llega por mensajes del mini-OS;
// el bucle no sondea registros.

#include <eng/api/api.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/xlimited_scroll_layer.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/input_poll.hpp>
#include <eng/scene/dpf_plan.hpp>
#include <eng/scene/plan.hpp>

#include "caverna_art.hpp"

namespace {

namespace playfield = eng::playfield;

constexpr eng::u16 kTile = caverna_art::kTile;
constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 208u;
// El anillo necesita 2 tiles de staging para el walk horizontal: el copiado de
// cada columna escribe 16 filas de tile y las sobrantes deben caer fuera de la
// ventana visible. Con 208 (13 tiles) se veían como un bloque de terreno arriba.
constexpr eng::u16 kRingHeight = static_cast<eng::u16>(kHeight + 2u * kTile);
constexpr eng::u16 kMapWidth = caverna_art::kMapWidth;
constexpr eng::u16 kMapHeight = caverna_art::kMapHeight;
constexpr eng::u8 kPlanes = caverna_art::kPlanes;
constexpr eng::u16 kTilesetCount = 128u;

constexpr playfield::ScrollConsts kScroll {
	kTile, kTile, kRingHeight, static_cast<eng::u32>(kRingHeight) * kPlanes, kPlanes};
using MapView = eng::playfield::FixedTileLayerMap<kMapWidth>;
using Profile = playfield::ScrollProgressive;

constexpr eng::u16 kSolidFirst = 2u;
constexpr eng::u16 kSolidLast = 13u;
constexpr eng::s16 kGravity = 1;
constexpr eng::s16 kJump = -8;
constexpr eng::s16 kPlayerSize = 16;

/// `true` si la celda bloquea al jugador. Los tiles 0/1 son decoración y los
/// 2..13 son el terreno y las plataformas del tileset.
bool tile_is_solid(eng::u16 tx, eng::u16 ty) {
	if (tx >= kMapWidth || ty >= kMapHeight) return true;
	const eng::u16 tile =
		caverna_art::map_cells()[static_cast<eng::u32>(ty) * kMapWidth + tx];
	return tile >= kSolidFirst && tile <= kSolidLast;
}

/// Colisión de la caja 16×16 con el tilemap en coordenadas de mundo. Se usa
/// `>> 4` (tile 16 px) y no división en el bucle de juego.
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

constexpr eng::u8 kActorCount = 9u; // jugador + ocho enemigos

struct CavernaGame {
	playfield::XlimitedScene<kScroll, MapView, Profile> scene {};
	// Adapta la escena a la interfaz `ScrollLayer` que conduce el `App` (tipada, sin `void*`).
	playfield::XlimitedScrollLayer<playfield::XlimitedScene<kScroll, MapView, Profile>,
				       eng::amiga::AmigaBackend> layer {scene};
	playfield::XlimitedSceneConfigT<MapView> config {};
	// Cámara del motor (posición px que conduce el `App`).
	eng::s32 cam_x = 0;
	eng::s32 cam_y = 0;
	eng::s16 player_wx = 152; // posición de mundo, no de pantalla
	eng::s16 player_y = 144;
	eng::s16 player_vy = 0;
	bool on_ground = true;
	bool jump_prev = false;
	static constexpr eng::u8 kEnemyCount = 8u;
	eng::s16 enemy_x[kEnemyCount] {40, 88, 184, 232, 280, 336, 392, 440};
	eng::s16 enemy_y[kEnemyCount] {96, 72, 112, 80, 64, 120, 88, 104};
	eng::s16 enemy_base_y[kEnemyCount] {96, 72, 112, 80, 64, 120, 88, 104};
	eng::s8 enemy_dx[kEnemyCount] {2, -1, 2, -2, 1, -1, 2, -1};
	static constexpr eng::u8 kSpriteCount = kActorCount;
	static constexpr eng::u16 kSpriteBytes = 64u;
	using Director = eng::scene::SpriteDirector<static_cast<eng::u16>(kActorCount),
					    static_cast<eng::u16>(kActorCount), 1u, 1u, 16u, 4u>;
	Director m_dir {};
	// Pasada de sprites que conduce el `App` antes del scroll: emite/refresca los actores
	// y entrega placements + plan de BOB a la escena. No es una capa visual: el director
	// decide HW/BOB/CPU y la prioridad PF es `SpriteHint::sprite_priority` por actor.
	using SpritePass = eng::scene::SpriteDirectorPass<Director, decltype(scene),
							   eng::amiga::AmigaBackend>;
	SpritePass m_sprite_pass {m_dir, scene};
	eng::scene::SpriteId m_actor_ids[kSpriteCount] {};
	eng::Span<eng::u16> m_sprite_words {};
	eng::s16 m_player_sx = -1;
	eng::s16 m_player_sy = -1;
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
		if (sx > static_cast<eng::s32>(kWidth - kPlayerSize)) {
			return static_cast<eng::s16>(kWidth - kPlayerSize);
		}
		return static_cast<eng::s16>(sx);
	}

	[[nodiscard]] eng::s16 enemy_screen_x(eng::u8 i) const {
		return static_cast<eng::s16>(enemy_x[i] - cam_x);
	}

	void init_sprites() {
		auto words = scene.sprites().sprite_words();
		m_sprite_words = eng::Span<eng::u16> {words.data(), words.size()};
		m_sprite_words.fill(0u);
		caverna_art::write_actor_art(m_sprite_words, 0u, 0u, 0u);
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			caverna_art::write_actor_art(m_sprite_words, static_cast<eng::u8>(i + 1u),
						     static_cast<eng::u8>((i % 3u) + 1u), 0u);
		}
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
			// Canales HW 0..7 con preferencia estable; el noveno actor (slot 8) no
			// reserva canal: entra por multiplexado vertical o degrada a BOB.
			if (slot < 8u) desc.hint.channel = slot;
			desc.x = slot == 0u ? player_screen_x()
					    : enemy_screen_x(static_cast<eng::u8>(slot - 1u));
			desc.y = slot == 0u ? player_y : enemy_y[slot - 1u];
			m_actor_ids[slot] = m_dir.add(desc);
			if (!m_actor_ids[slot].valid()) {
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
				return;
			}
		}
		m_sprite_pass.request_emit();
	}

	/// Mantiene al jugador en una posición estable de pantalla y desplaza el
	/// mundo: la cámara no se mueve a la vez que el sprite, sino que lo sigue.
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
	}

	/// Cambia la pose de arte cada 8 frames. La posición se actualiza aparte en
	/// cada frame; este gate solo afecta al dibujo (alas/patas), no al sprite.
	void animate_enemies(eng::u32 frame) {
		if ((frame & 7u) != 0u) return;
		const eng::u8 phase = static_cast<eng::u8>((frame >> 3u) & 1u);
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			caverna_art::write_actor_art(m_sprite_words, static_cast<eng::u8>(i + 1u),
						     static_cast<eng::u8>((i % 3u) + 1u), phase);
		}
	}

	/// Patrulla y hace flotar a los enemigos en cada frame (sin gate de
	/// posición). La cámara ya está actualizada antes de refrescar los sprites.
	void update_enemies(eng::u32 frame) {
		constexpr eng::s8 kBob[8] {0, 1, 2, 1, 0, -1, -2, -1};
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			enemy_x[i] = static_cast<eng::s16>(enemy_x[i] + enemy_dx[i]);
			if (enemy_x[i] < 16) {
				enemy_x[i] = 16;
				enemy_dx[i] = static_cast<eng::s8>(-enemy_dx[i]);
			}
			if (enemy_x[i] > static_cast<eng::s16>(kMapWidth * kTile - 16u)) {
				enemy_x[i] = static_cast<eng::s16>(kMapWidth * kTile - 16u);
				enemy_dx[i] = static_cast<eng::s8>(-enemy_dx[i]);
			}
			const eng::u8 bob = static_cast<eng::u8>((frame + static_cast<eng::u32>(i) * 3u) & 7u);
			enemy_y[i] = static_cast<eng::s16>(enemy_base_y[i] + kBob[bob]);
			(void)m_dir.set_position(m_actor_ids[static_cast<eng::u8>(i + 1u)],
						 enemy_screen_x(i), enemy_y[i]);
		}
	}

	/// Resuelve el contacto jugador/enemigo con una reacción mínima: invierte la
	/// patrulla y deja al jugador en la posición horizontal previa.
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

	/// Inicializa el arte, la escena y los actores, y registra la capa en el `App`.
	void init(auto& app) {
		eng::os::input_enable(eng::os::InputMouse | eng::os::InputJoystick);
		caverna_art::build();

		// Plan de escena: un campo de scroll (BG) + un lienzo FG (rol `Foreground`,
		// `content = Canvas`). `apply_dpf_plan` siembra geometría, paleta y roles.
		playfield::ScrollPlan bg {};
		bg.viewport_w = kWidth;
		bg.viewport_h = kHeight;
		bg.tile_w = kTile;
		bg.tile_h = kTile;
		bg.planes = kPlanes;
		bg.display_height = kRingHeight;
		bg.tilemap.palette = eng::PaletteWords {caverna_art::palette().data(), 32u};
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
		// map = PF1 (cueva de fondo); map2 = PF2 (terreno delante).
		config.map.cells = caverna_art::background_cells();
		config.map.height = kMapHeight;
		config.map.wrap_x = 0u;
		config.map.wrap_y = 0u;
		config.map.empty_tile = 15u;
		config.map2.cells = caverna_art::map_cells();
		config.map2.height = kMapHeight;
		config.map2.wrap_x = 0u;
		config.map2.wrap_y = 0u;
		config.map2.empty_tile = 0u; // el tile 0 de PF2 es transparente
		config.tileset_count = kTilesetCount;
		config.fg_row_fn = &caverna_art::cave_row;    // PF1: muro de cueva
		config.bg_row_fn = &caverna_art::terrain_row; // PF2: terreno
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
		if (!app.add_sprite_pass(m_sprite_pass)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		ready = true;
		eng::amiga::mark_ready();
	}

	/// Lee entrada por mensajes, integra gravedad/salto y resuelve la colisión
	/// contra el tilemap antes de mover la cámara y refrescar los sprites.
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
		// 1 px/frame: con DPF 3+3 y ocho actores, 2 px/frame hacía que el frame
		// rozara el límite del campo (medido 40,8 fps); 1 px mantiene 50 fps.
		const eng::s16 dx =
			static_cast<eng::s16>((joy_right ? 1 : 0) - (joy_left ? 1 : 0) + mouse_dx);
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
		// La capa de sprites (conducida por el `App`) compone/refresca después de este
		// update y antes del scroll; el juego no publica placements ni copperlist.
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
