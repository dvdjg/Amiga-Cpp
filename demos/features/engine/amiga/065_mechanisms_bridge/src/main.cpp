// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/engine/amiga/065_mechanisms_bridge --debug   && bash ./tools/run/run-demo.sh demos/features/engine/amiga/065_mechanisms_bridge --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/engine/amiga/065_mechanisms_bridge --release && bash ./tools/run/run-demo.sh demos/features/engine/amiga/065_mechanisms_bridge --keep-running

// ============================================================================
// Demo 065: mecanismos de escenario — puente flexible, jugador y catapulta (F6).
// ============================================================================
//
// Tutorial de la capa de juego F6 (`ROADMAP_SPRITES_COMPLETO.md` §3.6):
//
//   - **Puente** (`scene::Rope<s32>` + `rope_step`): 12 eslabones con extremos anclados,
//     integración verlet y restricciones de distancia (2 pasadas). Cada eslabón se dibuja
//     como un BOB cuadrado; el puente se **deforma** al pisarlo y bajo los impulsos.
//   - **Jugador**: caja que camina de lado a lado; su gravedad y los **pies** se resuelven
//     contra la cuerda (`rope_find_contact` + `rope_resolve_player`): se apoya en la
//     superficie y **empuja** el segmento al caer (impulso repartido entre sus dos puntos).
//   - **Catapulta** (`scene::Catapult`): ciclo cargar → disparar; el **extremo** sale de la
//     tabla de seno (`catapult_tip`) y al cruzar el ángulo de suelta nace la **bola**, que
//     sigue una **trayectoria balística** cocinada (`scene::Trajectory` + follower).
//   - **Destructible**: la caja objetivo se rompe al impacto y deja **escombros** con una
//     trayectoria de caída (4 followers en paralelo).
//
// Fondo: playfield a cero con degradado de `COLOR00` por bandas de Copper; los BOB llevan
// **padding transparente** en su hoja (principio del FastCopy de F5) y se copian opacos
// (`$F0`): la propia copia tapa la posición anterior, sin borrado ni máscara. El padding
// de cada objeto es su **paso máximo por tick**.

#include <eng/api/api.hpp>
#include <eng/core/data/utf8.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/font5x7.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/os/os.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/scene/mechanisms.hpp>
#include <eng/scene/rope.hpp>
#include <eng/scene/trajectory.hpp>

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
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {
	0xffffffffu, 0, 0, 0, 0, { 0, 0, 0 }
};
}

namespace {

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Puente: 12 eslabones de x=40 a x=260, reposo en y=150.
constexpr eng::u8  kLinks = 12u;
constexpr eng::s32 kBridgeX0 = 40;
constexpr eng::s32 kBridgeStep = 20;
constexpr eng::s32 kBridgeY = 150;
constexpr eng::s32 kBridgeRest = 15; // rest_length (luz/11 = 20; 15 ⇒ catenario suave)
constexpr eng::s32 kGroundY = 200;   // suelo (los pies y las bolas se paran aquí)

// Jugador: caja 8x10 que camina de lado a lado.
constexpr eng::s32 kPlayerW = 8;
constexpr eng::s32 kPlayerH = 10;
constexpr eng::s32 kPlayerFeetHalf = 4;
constexpr eng::s16 kPlayerMinX = 16;
constexpr eng::s16 kPlayerMaxX = 280;

// Catapulta (pivote a la derecha) y su ciclo.
constexpr eng::s32 kCatPivotX = 288;
constexpr eng::s32 kCatPivotY = 170;
constexpr eng::s32 kCatArm = 26;
constexpr eng::s16 kCatRelease = 8;   // ángulo de suelta (unidades de tabla)
constexpr eng::u16 kCycle = 240u;     // ticks del ciclo (carga en 0, disparo en 120)
constexpr eng::u16 kFireTick = 120u;
constexpr eng::s32 kTargetY = 172;    // altura de la caja objetivo

// Padding de cada hoja = paso máximo por tick del objeto (principio FastCopy de F5).
constexpr eng::u16 kPlankPad = 8u;   // puente (poca velocidad) y brazo de la catapulta
constexpr eng::u16 kPlayerPad = 8u;  // jugador (vy tope 6)
constexpr eng::u16 kBallPad = 14u;   // bola (hasta ~12 px/tick en la caída)
constexpr eng::u16 kTargetPad = 2u;  // objetivo (estático)

// Paleta: COLOR00 = fondo (degradado por Copper); COLOR01 = claro; COLOR03 = cálido.
constexpr eng::Palette32 kPalette {{
	0x012, 0xfff, 0x000, 0xfc6, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
}};

struct MechanismsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006501u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(2048u, 16);
		m_sheet_block = backend.memory_manager().chip().reserve<eng::BobTag>(4096u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sheet_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006502u);
			return;
		}

		// Hojas con **padding transparente** (2 planos; valor en planos 0-1): el contenido
		// queda centrado y — para los objetos que NO se solapan con otros (eslabones del
		// puente, base y objetivo) — la copia **opaca** cubre su paso máximo (FastCopy). Los
		// objetos que SÍ se solapan (jugador sobre el puente, brazo de la catapulta, bola y
		// escombros sobre la cuerda) usan **cookie-cut con máscara** (`_m`): su parte
		// transparente no escribe y no borra al vecino; su estela se limpia con la pasada de
		// borrado de `erase_old_boxes`. Contrato: `FastBobLayer`-style solo sin solape.
		eng::u16* const sheets = m_sheet_block.view.as_words().data();
		const eng::u16 plank_words = padded_words(6u, 6u, kPlankPad);
		const eng::u16 player_words = padded_words(kPlayerW, kPlayerH, kPlayerPad);
		const eng::u16 ball_words = padded_words(4u, 4u, kBallPad);
		const eng::u16 target_words = padded_words(14u, 14u, kTargetPad);
		m_plank = make_padded(sheets, 0u, 6u, 6u, kPlankPad, 3u);
		m_player = make_padded(sheets, plank_words, kPlayerW, kPlayerH, kPlayerPad, 1u);
		m_ball = make_padded(sheets, static_cast<eng::u16>(plank_words + player_words), 4u, 4u,
				     kBallPad, 1u);
		m_target_bob = make_padded(sheets,
					   static_cast<eng::u16>(plank_words + player_words + ball_words),
					   14u, 14u, kTargetPad, 3u);
		// Máscaras de los objetos que se solapan (tras las hojas de contenido).
		const eng::u16 mask_base =
			static_cast<eng::u16>(plank_words + player_words + ball_words + target_words);
		const eng::u16 plank_mask_words = mask_sheet_words(6u, 6u, kPlankPad);
		const eng::u16 player_mask_words = mask_sheet_words(kPlayerW, kPlayerH, kPlayerPad);
		m_plank_m = make_masked(sheets, 0u, mask_base, 6u, 6u, kPlankPad, 3u);
		m_player_m = make_masked(sheets, plank_words,
					 static_cast<eng::u16>(mask_base + plank_mask_words), kPlayerW,
					 kPlayerH, kPlayerPad, 1u);
		m_ball_m = make_masked(sheets, static_cast<eng::u16>(plank_words + player_words),
				       static_cast<eng::u16>(mask_base + plank_mask_words +
							     player_mask_words),
				       4u, 4u, kBallPad, 1u);

		// Puente: cadena recta con extremos anclados.
		for (eng::u8 i = 0u; i < kLinks; ++i) {
			m_points[i].p.v[0] = kBridgeX0 + static_cast<eng::s32>(i) * kBridgeStep;
			m_points[i].p.v[1] = kBridgeY;
			m_points[i].prev = m_points[i].p;
			m_points[i].pinned = (i == 0u || i + 1u == kLinks);
		}
		m_rope.points = eng::Span<eng::scene::RopePoint<eng::s32>> {m_points, kLinks};
		m_rope.rest_length = kBridgeRest;
		m_rope.gravity = 1;
		m_rope.iterations = 2u;

		// Catapulta.
		m_catapult.pivot.v[0] = kCatPivotX;
		m_catapult.pivot.v[1] = kCatPivotY;
		m_catapult.arm_length = kCatArm;
		eng::scene::catapult_load(m_catapult);

		// **Trayectoria balística** (relativa al punto de suelta): x retrocede 4 px/tick y
		// la Y sube con `v0` y cae con la gravedad (entera, cocinada una vez).
		build_ballistic();
		// Trayectoria de caída de los escombros.
		for (eng::u16 i = 0u; i < kDebrisPoints; ++i) {
			m_fall_pts[i].p.v[0] = 0;
			m_fall_pts[i].p.v[1] = static_cast<eng::s16>((i * i) / 2);
			m_fall_pts[i].ticks = 1u;
		}
		m_fall_traj = eng::scene::Trajectory<eng::s16> {
			eng::Span<const eng::scene::PathPoint<eng::s16>> {m_fall_pts, kDebrisPoints},
			false, true};

		// Posición del objetivo: donde la bola vuelve a `kTargetY` (escaneo de la curva).
		eng::math::Vec<2, eng::s32> tip {};
		m_catapult.angle = kCatRelease;
		eng::scene::catapult_tip(m_catapult, tip);
		m_target_x = static_cast<eng::s16>(tip.v[0] + m_ball_pts[0].p.v[0]);
		for (eng::u16 i = 0u; i < kBallPoints; ++i) {
			if (tip.v[1] + m_ball_pts[i].p.v[1] >= kTargetY) {
				m_target_x = static_cast<eng::s16>(tip.v[0] + m_ball_pts[i].p.v[0]);
				break;
			}
		}
		m_target_y = static_cast<eng::s16>(kTargetY);
		m_catapult.angle = 0;

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00006503u);
			return;
		}
		draw_version_strip(0u);
		backend.takeover_display(m_copper_view);
		eng::debug::mark_ready(g_eng_run_status, 0x00006500u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (g_eng_run_status.state == static_cast<eng::u16>(eng::debug::RunState::Failed)) {
			return;
		}
		const eng::u16 frame = context.frame.frame_index;
		eng::debug::mark_frame(g_eng_run_status, frame);
		const eng::u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
		m_period_kcycles = (t0 - m_last_cycle) / 1000u;
		m_last_cycle = t0;

		// --- Mecánica por tick -------------------------------------------------------
		// Puente: simular siempre (12 puntos, coste mínimo); el jugador y las bolas lo
		// empujan con impulsos verlet.
		eng::scene::rope_step(m_rope);

		// Jugador: gravedad, avance lateral con rebote en los extremos y contacto.
		m_player_vy = static_cast<eng::s32>(m_player_vy + 1); // gravedad
		if (m_player_vy > 6) m_player_vy = 6;                 // velocidad de caída tope
		m_player_x = static_cast<eng::s16>(m_player_x + m_player_dir);
		if (m_player_x <= kPlayerMinX || m_player_x >= kPlayerMaxX) {
			m_player_dir = static_cast<eng::s16>(-m_player_dir);
		}
		m_player_y = static_cast<eng::s16>(m_player_y + m_player_vy);
		eng::math::Vec<2, eng::s32> feet {};
		feet.v[0] = static_cast<eng::s32>(m_player_x) + kPlayerFeetHalf;
		feet.v[1] = static_cast<eng::s32>(m_player_y) + kPlayerH;
		eng::s32 vy = m_player_vy;
		const auto contact = eng::scene::rope_find_contact(m_rope, feet, static_cast<eng::s32>(4));
		eng::scene::rope_resolve_player(m_rope, feet, vy, contact, 0u);
		if (feet.v[1] >= kGroundY) { // suelo (fuera del vano): se apoya y camina
			feet.v[1] = kGroundY;
			vy = 0;
		}
		m_player_y = static_cast<eng::s16>(feet.v[1] - kPlayerH);
		m_player_vy = vy;

		// Catapulta: ciclo cargar/disparar; la bola nace al cruzar el ángulo de suelta.
		const eng::u16 phase = static_cast<eng::u16>(context.frame.frame_index % kCycle);
		if (phase == 0u) {
			eng::scene::catapult_load(m_catapult);
		}
		if (phase == kFireTick) {
			eng::scene::catapult_fire(m_catapult);
		}
		const bool was_firing = m_catapult.firing;
		eng::scene::catapult_update(m_catapult);
		if (was_firing && m_catapult.angle >= kCatRelease && !m_ball_active) {
			eng::math::Vec<2, eng::s32> tip {};
			eng::scene::catapult_tip(m_catapult, tip);
			m_ball_follower = eng::scene::TrajectoryFollower<eng::s16> {};
			m_ball_follower.origin.v[0] = static_cast<eng::s16>(tip.v[0]);
			m_ball_follower.origin.v[1] = static_cast<eng::s16>(tip.v[1]);
			m_ball_active = true;
		}
		if (m_ball_active) {
			eng::math::Vec<2, eng::s16> pos {};
			eng::scene::trajectory_advance(m_ball_traj, m_ball_follower, 1u, pos);
			m_ball_x = pos.v[0];
			m_ball_y = pos.v[1];
			// Impacto con la caja objetivo: la rompe y suelta 4 escombros.
			if (m_target_active && m_ball_x >= m_target_x - 14 && m_ball_x <= m_target_x + 4 &&
			    m_ball_y >= m_target_y - 4 && m_ball_y <= m_target_y + 14) {
				m_target_active = false;
				m_respawn_timer = 300u;
				for (eng::u8 i = 0u; i < 4u; ++i) {
					eng::scene::TrajectoryFollower<eng::s16>& f = m_debris[i];
					f = eng::scene::TrajectoryFollower<eng::s16> {};
					f.origin.v[0] = static_cast<eng::s16>(m_target_x + (i & 1u ? 6 : 0));
					f.origin.v[1] = static_cast<eng::s16>(m_target_y + (i >= 2u ? 6 : 0));
				}
				m_debris_active = true;
			}
			if (m_ball_follower.finished || m_ball_y >= kGroundY) {
				m_ball_active = false;
			}
		}
		if (m_debris_active) {
			bool any = false;
			for (eng::u8 i = 0u; i < 4u; ++i) {
				eng::math::Vec<2, eng::s16> pos {};
				eng::scene::trajectory_advance(m_fall_traj, m_debris[i], 1u, pos);
				m_debris_x[i] = pos.v[0];
				m_debris_y[i] = pos.v[1];
				if (!m_debris[i].finished && pos.v[1] < kGroundY) any = true;
			}
			m_debris_active = any;
		}
		if (!m_target_active && m_respawn_timer != 0u) {
			--m_respawn_timer;
			if (m_respawn_timer == 0u) {
				m_target_active = true;
			}
		}

		// --- Emisión -----------------------------------------------------------------
		// La copperlist es **estática** (display + paleta + bandas): se emitió UNA vez en
		// `init` (`build_copper` + `takeover_display`). Aquí solo se ejecuta el plan de
		// blits del frame (**async**: la cadena avanza por IRQ mientras la CPU repinta la
		// franja o espera el VBlank) y, cada 32 frames, la telemetría de la franja.
		backend.frame_plan_async_wait(); // el plan del frame anterior ya no se toca
		m_frame_plan.clear();
		erase_old_boxes();
		emit_bobs();
		if (!backend.execute_frame_plan_async(m_frame_plan)) {
			(void)backend.execute_frame_plan(m_frame_plan);
		}
		g_eng_run_status.detail = m_frame_plan.ok() ? m_frame_plan.blit_job_count() : 0x6510u;
		m_update_kcycles = (eng::debug::DebugPeripheral::cycle_counter() - t0) / 1000u;
		if ((frame & 31u) == 0u) {
			draw_version_strip(frame);
		}
		{
			auto& tel = g_eng_frame_telemetry;
			tel.frame = frame;
			tel.blit_jobs = m_frame_plan.ok()
						? static_cast<eng::u16>(m_frame_plan.blit_job_count())
						: 0u;
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static constexpr eng::u16 kBallPoints = 64u;
	static constexpr eng::u16 kDebrisPoints = 32u;

	/// Palabras de una hoja planar con padding (`w`/`h` son las del **contenido**; la hoja
	/// crece `pad` por lado). Contrato compacto del engine: **una palabra de guarda por
	/// fila** (`base_words + 1`; `bob_detail::sheet_row_bytes`).
	[[nodiscard]] static constexpr eng::u16 padded_words(eng::u16 w, eng::u16 h, eng::u16 pad) {
		const eng::u16 pw = static_cast<eng::u16>(w + 2u * pad);
		const eng::u16 ph = static_cast<eng::u16>(h + 2u * pad);
		// **2 planos**: los valores de contenido (1 y 3) solo usan los bits 0-1. Declarar 4
		// planos cuadruplicaba los blits por dibujo en un target planar (84 blits/frame en
		// sync: la demo no pasaba de 17.6 fps).
		return static_cast<eng::u16>((((pw + 15u) / 16u) + 1u) * 2u * ph);
	}

	/// Escribe una hoja planar con **padding transparente** (contenido centrado, valor en
	/// planos 0-1) y devuelve su `Bob` (tamaño = contenido + 2·pad). El dibujo se hace en
	/// `(x - pad, y - pad)`; la copia opaca cubre la posición anterior.
	eng::graphics::Bob make_padded(eng::u16* sheets, eng::u16 word_off, eng::u16 w, eng::u16 h,
				       eng::u16 pad, eng::u16 value) {
		const eng::u16 pw = static_cast<eng::u16>(w + 2u * pad);
		const eng::u16 ph = static_cast<eng::u16>(h + 2u * pad);
		const eng::u16 words = padded_words(w, h, pad);
		for (eng::u16 i = 0u; i < words; ++i) {
			sheets[word_off + i] = 0u;
		}
		const eng::u16 hw = static_cast<eng::u16>((pw + 15u) / 16u);
		const eng::u16 row_words = static_cast<eng::u16>(hw + 1u); // base + guarda
		for (eng::u16 row = pad; row < static_cast<eng::u16>(pad + h); ++row) {
			for (eng::u16 p = 0u; p < 2u; ++p) {
				if ((value & (1u << p)) == 0u) continue;
				for (eng::u16 col = pad; col < static_cast<eng::u16>(pad + w); ++col) {
					// Solo las columnas del contenido (bit 15 = columna izquierda).
					const eng::u16 wi = static_cast<eng::u16>(col / 16u);
					const eng::u16 bit = static_cast<eng::u16>(15u - (col % 16u));
					sheets[word_off + p * row_words * ph + row * row_words + wi] |=
						static_cast<eng::u16>(1u << bit);
				}
			}
		}
		eng::graphics::Visual v {};
		v.pixels = eng::Span<const eng::u16> {sheets + word_off, words};
		v.w = pw;
		v.h = ph;
		v.bitplanes = 2u;
		v.frame_count = 1u;
		eng::graphics::Bob b = eng::scene::bob_from_visual(
			v, eng::graphics::BobLayout::Planar, eng::scene::TransparencyMode::ColorKey0);
		// Camino de esta demo: **copia opaca** ($F0) sobre hoja con padding (sin borrado).
		b.draw = eng::graphics::BobDraw::Opaque;
		b.erase = eng::graphics::BobErase::None;
		return b;
	}

	/// Palabras de una hoja de **máscara** (1 plano, con guarda por fila).
	[[nodiscard]] static constexpr eng::u16 mask_sheet_words(eng::u16 w, eng::u16 h, eng::u16 pad) {
		const eng::u16 pw = static_cast<eng::u16>(w + 2u * pad);
		const eng::u16 ph = static_cast<eng::u16>(h + 2u * pad);
		return static_cast<eng::u16>((((pw + 15u) / 16u) + 1u) * ph);
	}

	/// Escribe la máscara (1 plano) de un contenido sólido `w×h` con padding transparente.
	static void write_mask_sheet(eng::u16* sheets, eng::u16 word_off, eng::u16 w, eng::u16 h,
				     eng::u16 pad) {
		const eng::u16 pw = static_cast<eng::u16>(w + 2u * pad);
		const eng::u16 ph = static_cast<eng::u16>(h + 2u * pad);
		const eng::u16 words = mask_sheet_words(w, h, pad);
		for (eng::u16 i = 0u; i < words; ++i) {
			sheets[word_off + i] = 0u;
		}
		const eng::u16 row_words = static_cast<eng::u16>(((pw + 15u) / 16u) + 1u);
		for (eng::u16 row = pad; row < static_cast<eng::u16>(pad + h); ++row) {
			for (eng::u16 col = pad; col < static_cast<eng::u16>(pad + w); ++col) {
				const eng::u16 wi = static_cast<eng::u16>(col / 16u);
				const eng::u16 bit = static_cast<eng::u16>(15u - (col % 16u));
				sheets[word_off + row * row_words + wi] |=
					static_cast<eng::u16>(1u << bit);
			}
		}
	}

	/// Bob **cookie-cut con la máscara en plano aparte** (`SeparatePlane`): la parte
	/// transparente **no escribe**, así que este camino NO borra a los vecinos — es el que
	/// exige el contrato cuando los objetos se solapan (jugador sobre el puente, brazo de la
	/// catapulta consigo mismo, bola/escombros sobre los eslabones). La posición anterior se
	/// limpia con una **pasada de borrado** antes de dibujar (`erase_old_boxes`).
	eng::graphics::Bob make_masked(eng::u16* sheets, eng::u16 sheet_off, eng::u16 mask_off,
				       eng::u16 w, eng::u16 h, eng::u16 pad, eng::u16 value) {
		eng::graphics::Bob b = make_padded(sheets, sheet_off, w, h, pad, value);
		write_mask_sheet(sheets, mask_off, w, h, pad);
		b.draw = eng::graphics::BobDraw::CookieCut;
		b.mask_pack = eng::graphics::BobMaskPack::SeparatePlane;
		b.mask = eng::ChipView<eng::BobTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(sheets + mask_off)),
			static_cast<eng::u32>(mask_sheet_words(w, h, pad) * 2u)};
		return b;
	}

	void build_ballistic() {		// x = -4·i; y = -(v0·i − i²/4) con v0 = 12 (parábola entera, cocinada una vez).
		for (eng::u16 i = 0u; i < kBallPoints; ++i) {
			m_ball_pts[i].p.v[0] = static_cast<eng::s16>(-4 * static_cast<eng::s32>(i));
			m_ball_pts[i].p.v[1] = static_cast<eng::s16>(
				-(12 * static_cast<eng::s32>(i) - (static_cast<eng::s32>(i) * i) / 4));
			m_ball_pts[i].ticks = 1u;
		}
		m_ball_traj = eng::scene::Trajectory<eng::s16> {
			eng::Span<const eng::scene::PathPoint<eng::s16>> {m_ball_pts, kBallPoints}, false,
			true};
	}

	/// **Pasada de borrado** (antes de dibujar): limpia la caja de la posición ANTERIOR de
	/// los objetos cookie-cut. Con todas las cajas borradas antes de ningún dibujo, los
	/// solapes entre objetos del frame son correctos (y la cuerda se redibuja después).
	void erase_old_boxes() {
		if (m_player_prev_valid) {
			(void)eng::graphics::bob_erase_box(m_frame_plan, m_player_m,
							   static_cast<eng::u16>(kPlayerW + 2u),
							   static_cast<eng::u16>(kPlayerH + 2u),
							   m_player_prev_x, m_player_prev_y, m_target);
		}
		if (m_arm_prev_valid) {
			(void)eng::graphics::bob_erase_box(m_frame_plan, m_plank_m,
							   static_cast<eng::u16>(m_arm_prev_w),
							   static_cast<eng::u16>(m_arm_prev_h), m_arm_prev_x,
							   m_arm_prev_y, m_target);
		}
		if (m_ball_prev_valid) {
			(void)eng::graphics::bob_erase_box(m_frame_plan, m_ball_m, 6u, 6u, m_ball_prev_x,
							   m_ball_prev_y, m_target);
		}
		if (m_debris_prev_valid) {
			for (eng::u8 i = 0u; i < 4u; ++i) {
				(void)eng::graphics::bob_erase_box(m_frame_plan, m_ball_m, 6u, 6u,
								   m_debris_prev_x[i],
								   m_debris_prev_y[i], m_target);
			}
		}
	}

	void emit_bobs() {
		// **Un solo pase**: copia opaca en `(x - pad, y - pad)`; el padding transparente de
		// la hoja tapa la posición anterior (principio del FastCopy, F5). Sin borrado,
		// sin máscara y sin save-under; el `pad` de cada objeto es su paso máximo/tick.
		auto draw = [&](const eng::graphics::Bob& bob, eng::s16 x, eng::s16 y, eng::s16 pad) {
			const eng::s16 px = static_cast<eng::s16>(x - pad);
			const eng::s16 py = static_cast<eng::s16>(y - pad);
			if (px < 0 || py < 0) {
				return;
			}
			(void)eng::graphics::bob_draw(m_frame_plan, bob, 0u, px, py, m_target);
		};
		// Puente: un eslabón por punto.
		for (eng::u8 i = 0u; i < kLinks; ++i) {
			draw(m_plank, static_cast<eng::s16>(m_points[i].p.v[0] - 3),
			     static_cast<eng::s16>(m_points[i].p.v[1] - 3), kPlankPad);
		}
		// Jugador (cookie-cut: no borra la cuerda bajo sus pies; estela por pasada de borrado).
		draw(m_player_m, m_player_x, m_player_y, kPlayerPad);
		m_player_prev_x = static_cast<eng::s16>(m_player_x - 1);
		m_player_prev_y = static_cast<eng::s16>(m_player_y - 1);
		m_player_prev_valid = true;
		// Catapulta: base fija (fastcopy) + brazo "punteado" con **cookie-cut**: los 4
		// segmentos se solapan entre sí; con copia opaca el pad de uno borraría al anterior
		// (solo se veía el último). Con máscara se ensamblan y la estela la limpia la pasada
		// de borrado (caja AABB del brazo anterior).
		draw(m_plank, static_cast<eng::s16>(kCatPivotX - 8), static_cast<eng::s16>(kCatPivotY),
		     kPlankPad);
		eng::math::Vec<2, eng::s32> tip {};
		eng::scene::catapult_tip(m_catapult, tip);
		eng::s16 ax0 = static_cast<eng::s16>(kCatPivotX - 8);
		eng::s16 ay0 = static_cast<eng::s16>(kCatPivotY);
		eng::s16 ax1 = static_cast<eng::s16>(kCatPivotX - 2);
		eng::s16 ay1 = static_cast<eng::s16>(kCatPivotY + 6);
		for (eng::u8 k = 1u; k <= 3u; ++k) {
			const eng::s16 x = static_cast<eng::s16>(
				kCatPivotX + (tip.v[0] - kCatPivotX) * k / 4);
			const eng::s16 y = static_cast<eng::s16>(
				kCatPivotY + (tip.v[1] - kCatPivotY) * k / 4);
			draw(m_plank_m, static_cast<eng::s16>(x - 3), static_cast<eng::s16>(y - 3),
			     kPlankPad);
			ax0 = ax0 < static_cast<eng::s16>(x - 3) ? ax0 : static_cast<eng::s16>(x - 3);
			ay0 = ay0 < static_cast<eng::s16>(y - 3) ? ay0 : static_cast<eng::s16>(y - 3);
			ax1 = ax1 > static_cast<eng::s16>(x + 3) ? ax1 : static_cast<eng::s16>(x + 3);
			ay1 = ay1 > static_cast<eng::s16>(y + 3) ? ay1 : static_cast<eng::s16>(y + 3);
		}
		m_arm_prev_x = static_cast<eng::s16>(ax0 - 1);
		m_arm_prev_y = static_cast<eng::s16>(ay0 - 1);
		m_arm_prev_w = static_cast<eng::s16>(ax1 - ax0 + 2);
		m_arm_prev_h = static_cast<eng::s16>(ay1 - ay0 + 2);
		m_arm_prev_valid = true;
		// Bola, objetivo y escombros (bola y escombros cookie-cut: vuelan sobre la cuerda).
		if (m_ball_active) {
			draw(m_ball_m, static_cast<eng::s16>(m_ball_x - 2), static_cast<eng::s16>(m_ball_y - 2),
			     kBallPad);
			m_ball_prev_x = static_cast<eng::s16>(m_ball_x - 3);
			m_ball_prev_y = static_cast<eng::s16>(m_ball_y - 3);
			m_ball_prev_valid = true;
		} else {
			m_ball_prev_valid = false;
		}
		if (m_target_active) {
			draw(m_target_bob, m_target_x, m_target_y, kTargetPad);
		}
		if (m_debris_active) {
			for (eng::u8 i = 0u; i < 4u; ++i) {
				draw(m_ball_m, m_debris_x[i], m_debris_y[i], kBallPad);
				m_debris_prev_x[i] = static_cast<eng::s16>(m_debris_x[i] - 3);
				m_debris_prev_y[i] = static_cast<eng::s16>(m_debris_y[i] - 3);
			}
			m_debris_prev_valid = true;
		} else {
			m_debris_prev_valid = false;
		}
	}

	void build_display(eng::copper::SchedulerT<false>& sched) {
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		// Degradado de `COLOR00` por bandas de 32 líneas (fondo del playfield a cero).
		for (eng::u8 band = 1u; band < 7u; ++band) {
			sched.wait_line(static_cast<eng::u8>(44u + band * 32u));
			sched.move(eng::copper::Register::COLOR00,
				   static_cast<eng::u16>(0x012u + band * 0x111u));
		}
	}

	bool build_copper() {
		m_target = eng::graphics::make_bob_target(m_bitplane_block.mem_view_chip(), kBytesPerRow,
							  256u, kPlanes,
							  eng::graphics::BobLayout::Planar);
		eng::copper::SchedulerT<false> sched { m_copper_block };
		build_display(sched);
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_view = sched.chip_view();
		return m_copper_ok;
	}

	// ── Franja de versión/telemetría (norma del repo: 16 px de alto, 4 bpp, abajo) ──
	/// Rellena la franja y=240..255 con `value` (planos 0-3) escribiendo las palabras.
	void strip_fill(eng::u8 value) {
		eng::u16* const planes = m_bitplane_block.view.as_words().data();
		const eng::u16 wpr = static_cast<eng::u16>(kBytesPerRow / 2u);
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			const eng::u16 word = (value & (1u << p)) != 0u ? 0xffffu : 0x0000u;
			for (eng::s16 y = 240; y < 256; ++y) {
				eng::u16* const row = planes +
						     static_cast<eng::u32>(p) * (kPlaneBytes / 2u) +
						     static_cast<eng::u32>(y) * wpr;
				for (eng::u16 i = 0u; i < wpr; ++i) {
					row[i] = word;
				}
			}
		}
	}

	/// Escribe `value` en el píxel (x, y) de la franja (planos 0-3).
	void strip_set(eng::s16 x, eng::s16 y, eng::u8 value) {
		if (x < 0 || x >= 320 || y < 240 || y >= 256) {
			return;
		}
		eng::u16* const planes = m_bitplane_block.view.as_words().data();
		const eng::u16 wpr = static_cast<eng::u16>(kBytesPerRow / 2u);
		for (eng::u8 p = 0u; p < kPlanes; ++p) {
			eng::u16* const row = planes + static_cast<eng::u32>(p) * (kPlaneBytes / 2u) +
					      static_cast<eng::u32>(y) * wpr;
			const eng::u16 bit = static_cast<eng::u16>(0x8000u >> (x & 15));
			if ((value & (1u << p)) != 0u) {
				row[x / 16] = static_cast<eng::u16>(row[x / 16] | bit);
			} else {
				row[x / 16] = static_cast<eng::u16>(row[x / 16] & ~bit);
			}
		}
	}

	/// Texto con la fuente 5x7 del engine dentro de la franja (avance 5 px).
	void strip_text(eng::s16 x, eng::s16 y, const char* text, eng::u8 color) {
		const eng::u8* p = reinterpret_cast<const eng::u8*>(text);
		for (;;) {
			const eng::u32 cp = eng::utf8::decode(p);
			if (cp == 0u) {
				break;
			}
			if (cp >= 32u) {
				for (eng::u8 r = 0u; r < eng::Font5x7::kRows; ++r) {
					const eng::u8 bits = eng::Font5x7::row(static_cast<eng::u16>(cp), r);
					for (eng::u8 c = 0u; c < 5u; ++c) {
						if ((bits & (0x10u >> c)) != 0u) {
							strip_set(static_cast<eng::s16>(x + c),
								  static_cast<eng::s16>(y + r), color);
						}
					}
				}
			}
			x = static_cast<eng::s16>(x + 5);
		}
	}

	/// Texto decimal con prefijo en la franja (sin stdlib).
	void strip_num(eng::s16 x, eng::s16 y, const char* prefix, eng::u32 value, eng::u8 color) {
		char buf[16];
		eng::u8 n = 0u;
		while (prefix[n] != '\0' && n + 1u < sizeof(buf)) {
			buf[n] = prefix[n];
			++n;
		}
		char tmp[10];
		eng::u8 t = 0u;
		do {
			tmp[t++] = static_cast<char>('0' + (value % 10u));
			value /= 10u;
		} while (value != 0u && t < 10u);
		while (t > 0u && n + 1u < sizeof(buf)) {
			buf[n++] = tmp[--t];
		}
		buf[n] = '\0';
		strip_text(x, y, buf, color);
	}

	/// Franja: nombre + versión + telemetría (F/U/T en k ciclos), repintada cada 32 frames.
	void draw_version_strip(eng::u16 frame) {
		strip_fill(4u); // fondo negro (COLOR04..)
		strip_text(4, 241, "065 MECHANISMS BRIDGE", 1u);
		char label[16];
		(void)eng::debug::version_label(eng::debug::Version {1u, 0u}, label,
						static_cast<eng::u8>(sizeof(label)));
		strip_text(4, 249, label, 3u);
		strip_num(96, 249, "F", frame, 1u);
		strip_num(160, 249, "U", m_update_kcycles, 1u);
		strip_num(240, 249, "T", m_period_kcycles, 1u);
	}

	bool m_copper_ok = false;
	eng::ChipView<eng::CopperTag> m_copper_view {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::graphics::BobTarget m_target {};
	eng::graphics::Bob m_plank {};
	eng::graphics::Bob m_player {};
	eng::graphics::Bob m_ball {};
	eng::graphics::Bob m_target_bob {};
	// Variantes **cookie-cut** (máscara en plano aparte) de los objetos que se solapan.
	eng::graphics::Bob m_plank_m {};
	eng::graphics::Bob m_player_m {};
	eng::graphics::Bob m_ball_m {};
	// Estado de la pasada de borrado (posiciones ANTERIORES, en cajas de contenido).
	bool m_player_prev_valid = false;
	eng::s16 m_player_prev_x = 0;
	eng::s16 m_player_prev_y = 0;
	bool m_arm_prev_valid = false;
	eng::s16 m_arm_prev_x = 0;
	eng::s16 m_arm_prev_y = 0;
	eng::s16 m_arm_prev_w = 0;
	eng::s16 m_arm_prev_h = 0;
	bool m_ball_prev_valid = false;
	eng::s16 m_ball_prev_x = 0;
	eng::s16 m_ball_prev_y = 0;
	bool m_debris_prev_valid = false;
	eng::s16 m_debris_prev_x[4] {};
	eng::s16 m_debris_prev_y[4] {};
	// Puente.
	eng::scene::RopePoint<eng::s32> m_points[kLinks] {};
	eng::scene::Rope<eng::s32> m_rope {};
	// Jugador.
	eng::s16 m_player_x = 24;
	eng::s16 m_player_y = 100;
	eng::s16 m_player_dir = 1;
	eng::s32 m_player_vy = 0;
	// Catapulta + bola + escombros + objetivo.
	eng::scene::Catapult<eng::s32> m_catapult {};
	eng::scene::PathPoint<eng::s16> m_ball_pts[kBallPoints] {};
	eng::scene::Trajectory<eng::s16> m_ball_traj {};
	eng::scene::TrajectoryFollower<eng::s16> m_ball_follower {};
	bool m_ball_active = false;
	eng::s16 m_ball_x = 0;
	eng::s16 m_ball_y = 0;
	eng::scene::PathPoint<eng::s16> m_fall_pts[kDebrisPoints] {};
	eng::scene::Trajectory<eng::s16> m_fall_traj {};
	eng::scene::TrajectoryFollower<eng::s16> m_debris[4] {};
	eng::s16 m_debris_x[4] {};
	eng::s16 m_debris_y[4] {};
	bool m_debris_active = false;
	bool m_target_active = true;
	eng::u16 m_respawn_timer = 0u;
	eng::s16 m_target_x = 100;
	eng::s16 m_target_y = kTargetY;
	// Telemetría de la franja.
	eng::u32 m_last_cycle = 0u;
	eng::u32 m_update_kcycles = 0u;
	eng::u32 m_period_kcycles = 0u;
	// Bloques.
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_sheet_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	MechanismsDemo game {};
	eng::Engine engine { backend, game };
	// La demo usa el **encadenado async** del `FramePlan` (su propia IRQ de fin de blit):
	// `run_frames` instala por defecto el `BackgroundPump` como servicio de blit y el slot es
	// exclusivo — sin esto, `execute_frame_plan_async` fallaría en silencio (bug 4 de la 066).
	engine.set_blit_service_enabled(false);
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
