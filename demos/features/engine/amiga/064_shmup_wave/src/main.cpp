// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/engine/amiga/064_shmup_wave --debug   && bash ./tools/run/run-demo.sh demos/features/engine/amiga/064_shmup_wave --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/engine/amiga/064_shmup_wave --release && bash ./tools/run/run-demo.sh demos/features/engine/amiga/064_shmup_wave --keep-running

// ============================================================================
// Demo 064: shmup mínimo - formaciones, rutas y timeline (F2 + F8).
// ============================================================================
//
// Tutorial de la capa de dinámica de juego (F2/F8 del roadmap de objetos):
//
//   - **Formación** (`scene::Formation` + `formation_update`): 5 naves entran escalonadas
//     con delays y offsets, siguiendo dos **rutas seno** en espejo (`gen_sine_vertical`).
//   - **Pool** (`scene::EntityPool`): las naves viven con `TrajectoryFollower` +
//     `CompositeState` (contenido de 1 parte) y se reciclan al salir de pantalla. Se
//     dibujan por **BOB** (contenido de 2 planos con máscara).
//   - **Timeline** (`util::Sequence` + `SequenceRunner`): una pista de eventos dispara
//     **ráfagas** de 3 balas cada 20 ticks; las balas van por **Sprite HW**
//     (`eng::SpriteScene`, contenido DAT/DATB) y se mueven hacia arriba hasta salir.
//
// Sin enemigos complejos: el objetivo es que el juego consuma formaciones/rutas/timeline y
// que las balas verticales usen los canales de sprite (el allocator multiplexa por Y).
//
// Fondo: gradiente vertical por Copper en COLOR00 (8 bandas, coste despreciable) y planos a
// cero: el borrado por caja de los BOB enemigos repinta color 0 puro, que aquí es el
// gradiente de esa línea.

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/bobs.hpp> // `scene::clear_box`

#include <exec/execbase.h>
#include <proto/exec.h>

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

// Display 320x256, 4 planos, contiguos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;
constexpr eng::u16 kArmLine = 32u;
constexpr eng::u16 kDisplayTop = 0x2cu;

// Enemigo: 16x16 planar a 2 planos + máscara (BOB). Fila = 1 base + 1 guarda.
constexpr eng::u16 kEnemyW = 16u;
constexpr eng::u16 kEnemyH = 16u;
constexpr eng::u16 kEnemyRowWords = 2u;
constexpr eng::u16 kEnemyPlaneWords = static_cast<eng::u16>(kEnemyH * kEnemyRowWords); // 32
constexpr eng::u16 kEnemyDataWords = static_cast<eng::u16>(kEnemyPlaneWords * 2u);      // 64
constexpr eng::u16 kEnemyMaskWords = kEnemyPlaneWords;

// Bala: Sprite HW de 16 px, 8 líneas DAT/DATB + terminador (Chip).
constexpr eng::u16 kBulletWords = static_cast<eng::u16>(8u * 2u + 2u);
constexpr eng::u16 kSpriteBlockWords = static_cast<eng::u16>(kBulletWords + 16u); // + reset

// Formación: 5 naves, delays escalonados y dos rutas seno en espejo.
constexpr eng::u8 kEnemies = 5u;
constexpr eng::u8 kRoutes = 2u;
constexpr eng::u16 kRoutePoints = 40u;
constexpr eng::u16 kBulletsMax = 8u;

// Paleta: COLOR00 = gradiente inferior (banda 0 se reescribe por Copper), COLOR01/02
// enemigos, COLOR17-19 = par 0/1 de las balas.
constexpr eng::Palette32 kPalette {{
	0x012, 0x3cf, 0xfff, 0x222, 0x333, 0x444, 0x555, 0x666,
	0x777, 0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee,
	0x000, 0x000, 0x000, 0xff0, // COLOR19 = amarillo de la bala
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

// Gradiente de COLOR00 por bandas (de azul oscuro abajo a negro).
constexpr eng::u16 kSkyBands[8] = {0x013, 0x012, 0x011, 0x011,
				   0x010, 0x010, 0x001, 0x001};

/// Máscara de bits de un rango de píxeles [x0, x1] (bit 15 = píxel 0, izquierda).
[[nodiscard]] constexpr eng::u16 bits(eng::u16 x0, eng::u16 x1) {
	eng::u16 m = 0u;
	for (eng::u16 x = x0; x <= x1; ++x) {
		m = static_cast<eng::u16>(m | (0x8000u >> x));
	}
	return m;
}

struct ShmupWaveDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006401u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_bob_block = backend.memory_manager().chip().reserve<eng::BobTag>(
			static_cast<eng::u32>(kEnemyDataWords + kEnemyMaskWords) * 2u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteBlockWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_bob_block.valid() || !m_sprite_block.valid() ||
		    !m_copper.begin(backend.memory_manager(), 2048u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006402u);
			return;
		}
		build_enemy_art();
		build_bullet_art();
		if (!setup_content()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006403u);
			return;
		}
		m_target = eng::graphics::make_bob_target(m_bitplane_block.mem_view_chip(),
							  kBytesPerRow, 256u, kPlanes,
							  eng::graphics::BobLayout::Planar);
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006404u);
			return;
		}
		m_copper.takeover(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x0000u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u16 f = context.frame.frame_index;
		eng::debug::mark_frame(g_eng_run_status, f);
		m_frame_plan.clear();

		// 1) Borra las cajas previas de los enemigos que ya entraron en el bitmap (los que
		// aún están por encima no se pintan: el engine rechaza el BOB fuera por arriba).
		for (eng::u16 i = 0u; i < kEnemies * 4u; ++i) {
			auto& s = m_pool.slots[i];
			if (s.active && s.prev_y >= 8 && s.prev_y < 256) {
				(void)eng::scene::clear_box(m_frame_plan, m_target,
							    static_cast<eng::s16>(s.prev_x - 8),
							    static_cast<eng::s16>(s.prev_y - 8),
							    kEnemyW, kEnemyH);
			}
		}

		// 2) Formación + pool: aparecen las naves con delay y siguen su ruta seno.
		eng::scene::formation_update(m_wave, m_wave_state, 1u,
					     [&](eng::usize, eng::u8 traj, eng::math::Vec<2, eng::s16> pos) {
						     (void)m_pool.spawn(eng::scene::EntityKind::Enemy,
									&m_enemy_vis,
									&m_routes[traj], traj,
									pos.x(), pos.y());
					     });
		m_pool.update(eng::Span<const eng::scene::Trajectory<eng::s16>> {m_routes, kRoutes}, 1u);
		// La ruta no es en bucle: al terminarla, la nave se retira del pool (reciclable).
		for (eng::u16 i = 0u; i < kEnemies * 4u; ++i) {
			if (m_pool.slots[i].active && m_pool.slots[i].traj.finished) {
				m_pool.kill(i);
			}
		}

		// 3) Timeline: cada evento recicla una ráfaga de 3 balas a una X que barre.
		m_runner.advance(m_level, 1u, [&](eng::u16 id, eng::u32) {
			if (id == 1u) {
				fire_volley();
			}
		});
		move_bullets();

		// 4) Emisión: enemigos a BOB (contenido de 1 parte) y balas a Sprite HW.
		for (eng::u16 i = 0u; i < kEnemies * 4u; ++i) {
			auto& s = m_pool.slots[i];
			if (s.active && s.visual != nullptr) {
				(void)eng::scene::composite_emit_bobs(*s.visual, s.anim, {},
								      m_frame_plan, m_target);
			}
		}
		eng::scene::ActorEmitContext ctx {};
		ctx.display_top = kDisplayTop;
		const auto r = m_bullets.emit(m_frame_plan, ctx);
		(void)r; // resumen (sprites/degraded) disponible para el HUD del juego

		// 5) Blits y copperlist del frame.
		(void)backend.execute_frame_plan(m_frame_plan);
		if (build_copper()) {
			m_copper.install(backend);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		(void)backend;
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Enemigo: rombo 16x16 a 2 planos (borde COLOR01, núcleo COLOR02) + máscara.
	void build_enemy_art() {
		eng::Words<eng::BobTag> bobs = m_bob_block.view.as_words();
		eng::u16* data = bobs.data();
		eng::u16* mask = bobs.data() + kEnemyDataWords;
		for (eng::u16 y = 0u; y < kEnemyH; ++y) {
			for (eng::u16 x = 0u; x < kEnemyW; ++x) {
				const eng::s32 dx = (x < 8u) ? static_cast<eng::s32>(8u - x)
							     : static_cast<eng::s32>(x - 7u);
				const eng::s32 dy = (y < 8u) ? static_cast<eng::s32>(8u - y)
							     : static_cast<eng::s32>(y - 7u);
				const eng::s32 man = dx + dy;
				if (man > 11) {
					continue;
				}
				const eng::u16 v = (man >= 8) ? 1u : 2u; // borde vs núcleo
				const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
				mask[y * kEnemyRowWords + 0u] =
					static_cast<eng::u16>(mask[y * kEnemyRowWords + 0u] | bit);
				data[(0u + y) * kEnemyRowWords + 0u] =
					((v & 1u) != 0u)
						? static_cast<eng::u16>(
							  data[(0u + y) * kEnemyRowWords + 0u] | bit)
						: data[(0u + y) * kEnemyRowWords + 0u];
				data[(kEnemyH + y) * kEnemyRowWords + 0u] =
					((v & 2u) != 0u)
						? static_cast<eng::u16>(
							  data[(kEnemyH + y) * kEnemyRowWords + 0u] | bit)
						: data[(kEnemyH + y) * kEnemyRowWords + 0u];
			}
			mask[y * kEnemyRowWords + 1u] = 0u; // guarda
			data[y * kEnemyRowWords + 1u] = 0u;
			data[(kEnemyH + y) * kEnemyRowWords + 1u] = 0u;
		}
	}

	/// Bala: barra vertical de 2 px en el centro (valor 3 -> COLOR17-19 del par).
	void build_bullet_art() {
		eng::Words<eng::SpriteTag> spr = m_sprite_block.view.as_words();
		for (eng::u16 y = 0u; y < 8u; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			if (y >= 1u && y <= 6u) {
				dat = static_cast<eng::u16>(bits(7u, 8u));
				datb = static_cast<eng::u16>(bits(7u, 8u));
			}
			spr[y * 2u + 0u] = dat;
			spr[y * 2u + 1u] = datb;
		}
		spr[kBulletWords - 2u] = 0u; // terminador
		spr[kBulletWords - 1u] = 0u;
	}

	/// Contenido del enemigo (1 parte BOB) + rutas seno + formación + timeline.
	bool setup_content() {
		eng::Words<eng::BobTag> bobs = m_bob_block.view.as_words();
		m_enemy_parts[0].visual.pixels = bobs.first(kEnemyDataWords).raw();
		m_enemy_parts[0].visual.mask =
			bobs.subspan(kEnemyDataWords, kEnemyMaskWords).raw();
		m_enemy_parts[0].visual.w = kEnemyW;
		m_enemy_parts[0].visual.h = kEnemyH;
		m_enemy_parts[0].visual.bitplanes = 2u;
		m_enemy_frame = eng::graphics::CompositeFrame {
			eng::Span<const eng::u8> {&m_enemy_part_frame, 1u}, {}, 1u, 0u};
		m_enemy_seq = eng::graphics::CompositeSequence {
			eng::Span<const eng::graphics::CompositeFrame> {&m_enemy_frame, 1u}, true, 0xffu};
		m_enemy_vis.parts = eng::Span<const eng::graphics::CompositePart> {m_enemy_parts, 1u};
		m_enemy_vis.sequences = eng::Span<const eng::graphics::CompositeSequence> {&m_enemy_seq, 1u};
		m_enemy_vis.anchor_x = 8;
		m_enemy_vis.anchor_y = 8;
		m_enemy_vis.bounds = eng::Box {0, 0, kEnemyW, kEnemyH};

		// Rutas seno en espejo (amplitud 56, paso Y 4; 40 puntos cubren la pantalla).
		const eng::u16 n0 = eng::scene::gen_sine_vertical(
			eng::Span<eng::scene::PathPoint<eng::s16>> {m_route_pts[0], kRoutePoints}, 56,
			4, kRoutePoints, 0u, 1u);
		const eng::u16 n1 = eng::scene::gen_sine_vertical(
			eng::Span<eng::scene::PathPoint<eng::s16>> {m_route_pts[1], kRoutePoints}, 56,
			4, kRoutePoints, 128u, 1u);
		m_routes[0] = eng::scene::Trajectory<eng::s16> {
			eng::Span<const eng::scene::PathPoint<eng::s16>> {m_route_pts[0], n0}, false,
			true};
		m_routes[1] = eng::scene::Trajectory<eng::s16> {
			eng::Span<const eng::scene::PathPoint<eng::s16>> {m_route_pts[1], n1}, false,
			true};

		for (eng::u8 i = 0u; i < kEnemies; ++i) {
			m_members[i].delay_ticks = static_cast<eng::u16>(i * 6u);
			m_members[i].offset.x() = static_cast<eng::s16>((static_cast<eng::s16>(i) - 2) * 28);
			m_members[i].offset.y() = 0;
			m_members[i].trajectory = static_cast<eng::u8>(i & 1u);
		}
		m_wave.members = eng::Span<const eng::scene::FormationMember<eng::s16>> {m_members,
											kEnemies};
		m_wave.origin.x() = 160;
		m_wave.origin.y() = -24;
		m_wave_state.active = true;

		// Timeline: ráfaga cada 24 ticks, en bucle de 240 (máx. 2 ráfagas vivas = 6 balas,
		// por debajo de los 8 canales de sprite).
		m_level.length = 240u;
		m_level.loop = true;
		for (eng::u8 i = 0u; i < 10u; ++i) {
			(void)m_level.events[0].add(static_cast<eng::u16>(24u + i * 24u), 1u);
		}
		m_runner.reset(true);

		// Balas como Sprite HW: contenido DAT/DATB de 16x8 en Chip.
		m_bullet_vis.pixels = m_sprite_block.view.as_words().first(kBulletWords).raw();
		m_bullet_vis.w = 16u;
		m_bullet_vis.h = 8u;
		m_bullet_vis.bitplanes = 2u;
		m_bullets.set_budget({8u, 2048u, 0u});
		// Pool FIJO de 8 balas (Sprite HW): se crean aquí y se reciclan por posición.
		for (eng::u8 s = 0u; s < kBulletsMax; ++s) {
			eng::scene::ActorDesc d {};
			d.visual = m_bullet_vis;
			d.x = static_cast<eng::s16>(40u + s * 34u);
			d.y = static_cast<eng::s16>(48u + s * 22u);
			d.preferred = eng::scene::Representation::Sprite;
			d.transparency = eng::scene::TransparencyMode::Opaque; // sprite: nativo
			d.background = eng::scene::BackgroundPolicy::None;
			m_bullet_ids[s] = m_bullets.add(d);
			if (!m_bullet_ids[s].valid()) {
				return false;
			}
		}
		return true;
	}

	/// **Ráfaga**: recicla 3 balas del pool fijo a la nueva X (y=abajo). El pool de 8
	/// actores se crea UNA vez en `setup_content` porque el alta dinámica desde `update`
	/// deja a los Sprite HW sin publicar en target (incidencia abierta con evidencia y
	/// plan en `docs/debugging/investigaciones/064-sprite-hw-creado-en-update-no-publica.md`);
	/// las balas se reciclan por posición, no por alta/baja.
	void fire_volley() {
		const eng::u16 base_x = static_cast<eng::u16>(48u + ((m_volley * 53u) % 208u));
		++m_volley;
		for (eng::u8 k = 0u; k < 3u; ++k) {
			const eng::u8 s = m_bullet_next;
			m_bullet_next = static_cast<eng::u8>((m_bullet_next + 1u) % kBulletsMax);
			auto a = m_bullets.store().get(m_bullet_ids[s]);
			if (!a.valid()) {
				continue;
			}
			a->desc.x = static_cast<eng::s16>(base_x + k * 16u - 16u);
			a->desc.y = 208;
		}
	}

	/// Mueve las balas hacia arriba (4 px/frame); al salir por arriba vuelven por abajo
	/// (flujo continuo, sin altas/bajas: reutiliza los mismos actores).
	void move_bullets() {
		for (eng::u8 s = 0u; s < kBulletsMax; ++s) {
			auto a = m_bullets.store().get(m_bullet_ids[s]);
			if (!a.valid()) {
				continue;
			}
			const eng::s16 ny = static_cast<eng::s16>(a->desc.y - 4);
			a->desc.y = (ny < static_cast<eng::s16>(kDisplayTop + 4u)) ? 240 : ny;
		}
	}

	/// Copperlist del frame: display, reset de canales, `SPREN`, paleta, gradiente de
	/// COLOR00 por bandas y armado de las balas (Sprite HW).
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper.inactive_block() };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		const eng::uintptr sprite_base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0u; c < 8u; ++c) {
			sched.move(static_cast<eng::copper::Register>(0x120u + c * 4u),
				   static_cast<eng::u16>(sprite_base >> 16));
			sched.move(static_cast<eng::copper::Register>(0x122u + c * 4u),
				   static_cast<eng::u16>(sprite_base & 0xffffu));
			sched.move(static_cast<eng::copper::Register>(0x142u + c * 8u), 0u);
			sched.move(static_cast<eng::copper::Register>(0x140u + c * 8u), 0u);
		}
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		// **Armado primero** (línea temprana 32, tras el reset): el WAIT de `present` cae
		// hacia delante. Los waits de las bandas van después, ya en orden creciente.
		m_bullets.present(sched, kArmLine); // Sprite HW del frame (una llamada)
		// Gradiente del cielo: COLOR00 por bandas (una vez por banda).
		eng::u16 line = 0x2cu;
		for (eng::u8 band = 0u; band < 8u; ++band) {
			sched.wait_line(static_cast<eng::u8>(line & 0xffu));
			sched.move(eng::copper::Register::COLOR00, kSkyBands[band]);
			line = static_cast<eng::u16>(line + 26u);
		}
		sched.emit_palette(kPalette.color, 1u, 31u); // COLOR00 va por bandas
		sched.wait_line(0xf8);
		sched.end();
		if (!sched.ok()) {
			return false;
		}
		m_copper.flip();
		return true;
	}

	eng::graphics::CompositePart m_enemy_parts[1] {};
	eng::u8 m_enemy_part_frame = 0u;
	eng::graphics::CompositeFrame m_enemy_frame {};
	eng::graphics::CompositeSequence m_enemy_seq {};
	eng::graphics::CompositeVisual m_enemy_vis {};
	eng::scene::PathPoint<eng::s16> m_route_pts[kRoutes][kRoutePoints] {};
	eng::scene::Trajectory<eng::s16> m_routes[kRoutes] {};
	eng::scene::FormationMember<eng::s16> m_members[kEnemies] {};
	eng::scene::Formation<eng::s16> m_wave {};
	eng::scene::FormationState m_wave_state {};
	eng::scene::EntityPool<kEnemies * 4u> m_pool {};
	eng::util::Sequence<float, 1u, 2u, 16u> m_level {};
	eng::util::SequenceRunner m_runner {};
	eng::graphics::Visual m_bullet_vis {};
	eng::SpriteScene<kBulletsMax> m_bullets {};
	eng::scene::ActorId m_bullet_ids[kBulletsMax] {};
	eng::u8 m_bullet_next = 0u;
	eng::u16 m_volley = 0u;
	eng::graphics::FramePlan m_frame_plan {};
	eng::graphics::BobTarget m_target {};
	eng::copper::DoubleBuffer m_copper {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::BobTag> m_bob_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	ShmupWaveDemo game {};
	eng::Engine engine { backend, game };
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
