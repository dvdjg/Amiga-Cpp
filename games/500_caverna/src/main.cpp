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
#include <eng/api/assets.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/xlimited_scroll_layer.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga/entry.hpp>
#include <eng/platform/amiga/input_poll.hpp>
#include <eng/scene/dpf_plan.hpp>
#include <eng/scene/plan.hpp>

#include "assets.manifest.hpp"
#include "caverna_art.hpp"
#include "caverna_gameplay.hpp"

namespace {

namespace playfield = eng::playfield;

/// Vista de bytes de un asset como palabras u16 (blobs big-endian en Amiga).
template <class Tag>
[[nodiscard]] eng::Span<const eng::u16> as_u16(eng::ByteView<Tag> bytes) noexcept {
	return eng::Span<const eng::u16> {
		reinterpret_cast<const eng::u16*>(bytes.data()), bytes.size() / 2u};
}

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

constexpr eng::s16 kGravity = 1;
constexpr eng::s16 kJump = -8;
constexpr eng::s16 kPlayerSize = caverna::kPlayerSize;
// Banda de pantalla sin scroll: el jugador se mueve dentro y la cámara solo le
// sigue al salir por un borde.
constexpr eng::s32 kDeadLeft = 96;
constexpr eng::s32 kDeadRight = 224;

/// Colisión de la caja del jugador contra el tilemap (lógica pura testeable).
[[nodiscard]] bool box_hits_solid(eng::s32 x, eng::s32 y) {
	return caverna::box_hits_solid(caverna_art::map_cells(), kMapWidth, kMapHeight, x, y,
				       kPlayerSize, kPlayerSize);
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
	static constexpr eng::u8 kEnemyCount = 8u;
	eng::s16 enemy_x[kEnemyCount] {40, 88, 184, 232, 280, 336, 392, 440};
	eng::s16 enemy_y[kEnemyCount] {96, 72, 112, 80, 64, 120, 88, 104};
	eng::s16 enemy_base_y[kEnemyCount] {96, 72, 112, 80, 64, 120, 88, 104};
	eng::s8 enemy_dx[kEnemyCount] {2, -1, 2, -2, 1, -1, 2, -1};
	bool enemy_alive[kEnemyCount] {true, true, true, true, true, true, true, true};
	eng::u8 lives = 3u;
	eng::u16 score = 0u;
	eng::u8 invuln = 0u;
	static constexpr eng::u8 kSpriteCount = kActorCount;
	using Director = eng::scene::SpriteDirector<static_cast<eng::u16>(kActorCount),
					    static_cast<eng::u16>(kActorCount), 1u, 1u, 16u, 4u>;
	eng::Assets m_assets {};
	Director m_dir {};
	// Pasada de sprites que conduce el `App` antes del scroll: emite/refresca los actores
	// y entrega placements + plan de BOB a la escena. No es una capa visual: el director
	// decide HW/BOB/CPU y la prioridad PF es `SpriteHint::sprite_priority` por actor.
	using SpritePass = eng::scene::SpriteDirectorPass<Director, decltype(scene),
							   eng::amiga::AmigaBackend>;
	SpritePass m_sprite_pass {m_dir, scene};
	eng::scene::SpriteId m_actor_ids[kSpriteCount] {};
	eng::Span<eng::u16> m_sprite_words {};
	eng::audio::AudioSystem* m_audio = nullptr;
	eng::Span<const eng::u8> m_sfx_jump {};
	eng::Span<const eng::u8> m_sfx_stomp {};
	eng::Span<const eng::u8> m_sfx_damage {};
	eng::Span<const eng::u8> m_sfx_land {};
	bool m_audio_ready = false;
	bool m_music_ok = false;

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

	/// Dispara un SFX del manifiesto si el mixer arrancó; sin audio no falla el juego.
	void play_sfx(eng::Span<const eng::u8> sample, eng::s16 priority) {
		if (m_audio == nullptr || !m_audio_ready || sample.empty()) return;
		(void)m_audio->play_sfx(eng::audio::SfxSample {sample}, priority,
					eng::audio::LoopMode::Once);
	}

	void init_sprites() {
		// Cuatro hojas de 128 B (dos poses de 16x16x2) desde los blobs del manifiesto.
		// La tabla de `Assets` no se usa para sprites: se copian al bloque Chip de la
		// escena y cada tipo comparte una vista de 64 palabras.
		const eng::u8* srcs[4] {caverna_assets::player_data(), caverna_assets::bat_data(),
					 caverna_assets::slime_data(), caverna_assets::crab_data()};
		auto words = scene.sprites().sprite_words();
		m_sprite_words = eng::Span<eng::u16> {words.data(), words.size()};
		if (m_sprite_words.size() < 4u * 64u) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		eng::scene::VisualId type_visual[4] {};
		for (eng::u8 type = 0u; type < 4u; ++type) {
			eng::u8* dst = reinterpret_cast<eng::u8*>(m_sprite_words.data() +
								 static_cast<eng::usize>(type) * 64u);
			for (eng::usize i = 0u; i < 128u; ++i) dst[i] = srcs[type][i];
			eng::graphics::Visual visual {};
			visual.pixels = eng::Span<const eng::u16> {
				m_sprite_words.data() + static_cast<eng::usize>(type) * 64u, 64u};
			visual.w = 16u;
			visual.h = 16u;
			visual.bitplanes = 2u;
			visual.frame_count = 2u;
			visual.frame_stride = 64u;
			type_visual[type] = m_dir.add_visual(visual);
			if (!type_visual[type].valid()) {
				eng::amiga::mark_failed(eng::debug::BootFail::Game);
				return;
			}
		}
		for (eng::u8 slot = 0u; slot < kSpriteCount; ++slot) {
			const eng::u8 type = slot == 0u ? 0u : static_cast<eng::u8>((slot - 1u) % 3u + 1u);
			eng::scene::SpriteDesc desc {};
			desc.content = eng::scene::SpriteVisualRef::simple(type_visual[type]);
			desc.animation = nullptr;
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

	/// Cámara con dead-zone: el jugador puede moverse dentro de la banda
	/// `[kDeadLeft, kDeadRight]` sin que el scroll se mueva; al salir, la cámara
	/// le sigue manteniendo el borde. Se acota al mundo.
	void follow_camera() {
		cam_x = caverna::dead_zone_camera(player_wx, cam_x,
						  static_cast<eng::s32>(kMapWidth * kTile),
						  kWidth, kDeadLeft, kDeadRight);
		cam_y = 0;
	}

	void place_player() {
		const eng::s16 sx = player_screen_x();
		if (sx == m_player_sx && player_y == m_player_sy) return;
		if (!m_dir.set_position(m_actor_ids[0], sx, player_y)) return;
		m_player_sx = sx;
		m_player_sy = player_y;
	}


	/// Patrulla y hace flotar a los enemigos en cada frame (sin gate de
	/// posición). La cámara ya está actualizada antes de refrescar los sprites.
	void update_enemies(eng::u32 frame) {
		constexpr eng::s8 kBob[8] {0, 1, 2, 1, 0, -1, -2, -1};
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			if (!enemy_alive[i]) continue;
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

	/// Da de baja un enemigo: deja de patrullar y se retira del director. La
	/// pasada pedirá una emisión completa (cambia la estructura de placements).
	void kill_enemy(eng::u8 i) {
		enemy_alive[i] = false;
		(void)m_dir.remove(m_actor_ids[static_cast<eng::u8>(i + 1u)]);
		m_actor_ids[static_cast<eng::u8>(i + 1u)] = {};
		score = static_cast<eng::u16>(score + 100u);
		play_sfx(m_sfx_stomp, 4);
		m_sprite_pass.request_emit();
	}

	/// Clasifica el contacto: caer sobre el enemigo lo elimina y bota al jugador;
	/// tocarlo de lado resta una vida con invulnerabilidad temporal. Si se acaban
	/// las vidas, el jugador vuelve al inicio del nivel.
	void resolve_enemy_hits(eng::s16 old_wx) {
		const eng::Box player { player_wx, player_y, kPlayerSize, kPlayerSize };
		for (eng::u8 i = 0u; i < kEnemyCount; ++i) {
			if (!enemy_alive[i]) continue;
			const eng::Box enemy { enemy_x[i], enemy_y[i], kPlayerSize, kPlayerSize };
			const caverna::EnemyHit hit =
				caverna::classify_enemy_hit(player, player_vy, enemy);
			if (hit == caverna::EnemyHit::Stomp) {
				kill_enemy(i);
				player_vy = -6;
				on_ground = false;
				return;
			}
			if (hit == caverna::EnemyHit::Damage && invuln == 0u) {
				lives = static_cast<eng::u8>(lives > 0u ? lives - 1u : 0u);
				invuln = 60u;
				play_sfx(m_sfx_damage, 5);
				player_wx = old_wx;
				player_vy = -4;
				on_ground = false;
				if (lives == 0u) {
					lives = 3u;
					player_wx = 152;
					player_y = 144;
					cam_x = 0;
				}
				return;
			}
		}
	}

	/// Inicializa el arte, la escena y los actores, y registra la capa en el `App`.
	void init(auto& app) {
		eng::os::input_enable(eng::os::InputMouse | eng::os::InputJoystick);
		// Assets por manifiesto: la demo no calcula arte ni copia blobs a mano.
		m_assets.bind(app.device().memory_manager());
		if (!caverna_assets::register_assets(m_assets)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		const bool art_ok = caverna_art::bind(
			as_u16(m_assets.view<eng::WorkTag>("cave_rows")),
			as_u16(m_assets.view<eng::WorkTag>("terrain_rows")),
			as_u16(m_assets.view<eng::MapCellsTag>("map_pf2")),
			as_u16(m_assets.view<eng::MapCellsTag>("map_pf1")));
		if (!art_ok) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}

		// Plan de escena: un campo de scroll (BG) + un lienzo FG (rol `Foreground`,
		// `content = Canvas`). `apply_dpf_plan` siembra geometría, paleta y roles.
		playfield::ScrollPlan bg {};
		bg.viewport_w = kWidth;
		bg.viewport_h = kHeight;
		bg.tile_w = kTile;
		bg.tile_h = kTile;
		bg.planes = kPlanes;
		bg.display_height = kRingHeight;
		const eng::Span<const eng::u16> palette = m_assets.palette("pal");
		bg.tilemap.palette = eng::PaletteWords {palette.data(), 32u};
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
		config.sprite_data_bytes = 4u * 128u;
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
		config.sprites_armed = false;
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
		// Audio: música P61 del manifiesto y cuatro SFX cortos en el mixer (AUD0).
		m_audio = &app.audio();
		m_sfx_jump = m_assets.audio("sfx_jump");
		m_sfx_stomp = m_assets.audio("sfx_stomp");
		m_sfx_damage = m_assets.audio("sfx_damage");
		m_sfx_land = m_assets.audio("sfx_land");
		m_music_ok = app.audio().play_music(m_assets.music("mod"));
		m_audio_ready = app.audio().init(app.memory_manager());
		if (!m_music_ok && !m_audio_ready) {
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
		// Fallback de hardware: los mensajes del mini-OS pueden no cubrir todos los
		// backends de entrada; el joystick del puerto 2 se lee también directamente.
		eng::input::InputAggregator& hw = app.input();
		eng::amiga::poll_input(hw);
		joy_left = joy_left || hw.pad1.left;
		joy_right = joy_right || hw.pad1.right;
		joy_up = joy_up || hw.pad1.up;
		joy_down = joy_down || hw.pad1.down;
		if (hw.pad1.fire) joy_fire = 1u;
		// 2 px/frame: el parcheo por placement (incluidos los rearmes de un canal
		// multiplexado) mantiene el frame dentro del campo; medido 49,87 fps con
		// entrada real sostenida.
		const eng::s16 dx =
			static_cast<eng::s16>((joy_right ? 2 : 0) - (joy_left ? 2 : 0) + mouse_dx);
		if (invuln > 0u) --invuln;
		const bool jump_down = (joy_fire != 0u) || ((mouse_buttons & 1u) != 0u) || joy_up;
		// Mantener pulsado salta en cuanto se toca el suelo: el juego es un
		// plataformas simple y así la captura por frames puede ver el salto.
		const bool was_ground = on_ground;
		if (jump_down && on_ground) {
			player_vy = kJump;
			on_ground = false;
			play_sfx(m_sfx_jump, 3);
		}

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

		if (!was_ground && on_ground) play_sfx(m_sfx_land, 1);
		resolve_enemy_hits(old_wx);
		follow_camera();
		place_player();
		update_enemies(app.frame());
		// La capa de sprites (conducida por el `App`) compone/refresca después de este
		// update y antes del scroll; el juego no publica placements ni copperlist.
	}

	/// El `App` materializa el mundo y conduce el motor; el juego solo publica telemetría.
	void render(auto& app) {
		if ((app.frame() & 15u) == 0u) {
			m_host_hud.draw(app.debug(), "CAVERNA DEMO 500", eng::debug::Version {1u, 0u},
					app.frame(), eng::debug::prof_clock(), nullptr);
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
