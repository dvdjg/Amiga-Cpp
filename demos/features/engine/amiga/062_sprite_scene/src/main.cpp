// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/features/engine/amiga/062_sprite_scene --debug   && bash ./tools/run/run-demo.sh demos/features/engine/amiga/062_sprite_scene --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/features/engine/amiga/062_sprite_scene --release && bash ./tools/run/run-demo.sh demos/features/engine/amiga/062_sprite_scene --keep-running

// ============================================================================
// Demo 062: fachada de sprites de nivel A — `eng::SpriteScene`.
// ============================================================================
//
// Tutorial: el juego **da de alta actores** (`ActorDesc`) y llama `emit`; el engine **compone**
// (HW sprites + `SpriteAllocator` + BOB fallback) **sin** que el juego declare los buffers de trabajo
// ni un `FramePlan`. Es la fachada del sistema de objetos NES (8 sprites/línea + overflow).
//
// ```cpp
// eng::scene::SpriteDirector<9, 9, 1, 1, 18, 4> sprites;
// sprites.add({.content = simple(visual), .x = x, .y = y});
// auto r = sprites.emit(plan, ctx);          // compone; los BOB van al plan
// sprites.present(sched, 32u, true);          // HW sprites a los registros (copper)
// ```
//
// Qué muestra: 9 actores de 16×16 en fila (y=100). Los 8 primeros caben en los canales 0..7 (parejas
// de color); el noveno **no cabe** y queda `as_bob`. El reparto se publica en
// `g_eng_run_status.detail` (`sprites << 8 | degradados`).
//
// NOTA DE CONTENIDO: un sprite hardware lee su DATA como DAT/DATB por línea; un BOB planar lee planos
// contiguos con máscara. El `Visual` tiene una sola vista, así que el degrado a BOB se **cuenta**
// pero no se dibuja (necesitaría un asset planar aparte). Fondo EHB estático (6 planos) con COLOR00 navy.

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
#include <eng/core/math/sinetable.hpp> // lissajous de los actores (regla 25)
#include <eng/debug/hud.hpp>           // HudFpsWindow (fps10)
#include <eng/debug/peripheral.hpp>    // contadores (telemetria de memoria)
#include <eng/debug/prof.hpp>          // prof_clock
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/backend.hpp>

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

namespace scene = eng::scene;

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// 9 actores de 16x16 en fila dentro de la ventana (x = 144, 176, ..., 400); solo 8 caben.
constexpr eng::u8  kActors = 9;
constexpr eng::u8  kSpriteHeight = 16;
constexpr eng::u16 kWordsPerLine = 2;   // DAT + DATB
// Cada instancia lleva su DATA (16 líneas x DAT/DATB) y, detrás, DOS palabras a cero que
// terminan el canal de DMA (AHRM 3.ª: "two all-zero words are placed at the end of the
// data structure to stop the DMA channel"). Sin ellas el canal sigue leyendo la
// instancia vecina.
constexpr eng::u16 kInstanceWords = static_cast<eng::u16>(kSpriteHeight) * kWordsPerLine + 2u; // 34
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kInstanceWords * kActors);              // 306
constexpr eng::u16 kY = 100;
constexpr eng::u16 kHpos0 = 144;
constexpr eng::u16 kHposStep = 32;

using Director = scene::SpriteDirector<
	kActors, kActors, 1u, 1u, static_cast<eng::u16>(kActors * 2u), 4u>;

// Fondo navy + parejas de color de sprite: COLOR17=rojo, COLOR21=verde,
// COLOR25=azul, COLOR29=amarillo (cada par de canales comparte su gama). Los indices 6 y 9
// son del **fondo estructurado** (regla 25: suelo verde y sol naranja) que el copper no toca.
constexpr eng::Palette32 kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x150, 0x777,
	0x888, 0xf80, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0xf00, 0x111, 0x111,   // COLOR16-19
	0x222, 0x0f0, 0x222, 0x222,   // COLOR20-23
	0x333, 0x00f, 0x333, 0x333,   // COLOR24-27
	0x444, 0xff0, 0x444, 0x444,   // COLOR28-31
}};

constexpr eng::SineTable<64, 64> kSin {};

/// Pixel del **fondo** en el bitmap (CPU, planos contiguos, 6 planos): pinta `color`
/// (índice lógico) en (x, y). Solo al init (fondo estático): no toca el coste del frame.
void put_bg(eng::u8* base, eng::u16 x, eng::u16 y, eng::u8 color) {
	const eng::u32 row = static_cast<eng::u32>(y) * kBytesPerRow + (x >> 3u);
	const eng::u8 bit = static_cast<eng::u8>(0x80u >> (x & 7u));
	for (eng::u8 p = 0u; p < kPlanes; ++p) {
		eng::u8* px = base + static_cast<eng::u32>(p) * kPlaneBytes + row;
		if ((color & (1u << p)) != 0u) {
			*px = static_cast<eng::u8>(*px | bit);
		} else {
			*px = static_cast<eng::u8>(*px & static_cast<eng::u8>(~bit));
		}
	}
}

struct SpriteAllocatorDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005401u);
			return;
		}

		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(1024, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005402u);
			return;
		}
		eng::Words<eng::SpriteTag> sprite_data = m_sprite_block.view.as_words();

		draw_background();
		build_sprite_sheet(sprite_data);
		if (!add_actors(sprite_data.as_const())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005404u);
			return;
		}
		if (!compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005405u);
			return;
		}

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005403u);
			return;
		}

		backend.takeover_display(m_copper_view);
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>("fps10"));
		eng::debug::DebugPeripheral::counter_name(1u, reinterpret_cast<eng::u32>("comp_ok"));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("comp_bad"));
		eng::debug::DebugPeripheral::counter_name(3u, reinterpret_cast<eng::u32>("split"));
		eng::debug::DebugPeripheral::counter_name(4u, reinterpret_cast<eng::u32>("composed"));
		m_fps_win.reset(context.frame.frame_index, eng::debug::prof_clock());
		eng::debug::mark_ready(
			g_eng_run_status,
			(static_cast<eng::u32>(m_sprites_in_hw) << 8u) |
				static_cast<eng::u32>(m_bob_count)
		);
	}

	/// Movimiento (regla 25) + recomposicion cada 4 frames: los actores recorren lissajous
	/// con x e y; la lista de copper se re-emite porque las posiciones de sprite
	/// (SPRxPOS/CTL) viajan en ella. Con los actores en lineas distintas el multiplexado de
	/// sprites admite los 9 en HW (0 degradados); el reparto se publica por contador.
	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u16 t = static_cast<eng::u16>(context.frame.frame_index);
		if ((t & 3u) == 0u) {
			for (eng::u8 i = 0; i < kActors; ++i) {
				auto a = m_dir.actor(m_ids[i]);
				if (!a.valid()) {
					continue;
				}
				const eng::u8 ph = static_cast<eng::u8>(
					(i * 9u + t) & 63u);
				a->desc.x = static_cast<eng::s16>(
					static_cast<eng::s16>(kHpos0 +
							      static_cast<eng::u16>(i) * kHposStep) +
					kSin[ph] * 24 / 64);
				a->desc.y = static_cast<eng::s16>(
					static_cast<eng::s16>(kY) +
					kSin[static_cast<eng::u8>((ph + 21u) & 63u)] * 18 / 64);
			}
			const bool composed = compose();
			// Telemetria de memoria: TODO actor representado (HW + degradados = 9). Con los
			// actores moviendose por lineas distintas el reparto pasa de 8+1 (estatico) a
			// 9+0 (multiplexado por el allocator); el invariante es la suma, no el reparto.
			if (composed &&
			    static_cast<eng::u8>(m_sprites_in_hw + m_bob_count) == kActors) {
				++m_comp_ok;
			} else {
				++m_comp_bad;
			}
			eng::debug::DebugPeripheral::counter_value(1u, m_comp_ok);
			eng::debug::DebugPeripheral::counter_value(2u, m_comp_bad);
			eng::debug::DebugPeripheral::counter_value(
				3u, (static_cast<eng::u32>(m_sprites_in_hw) << 8u) |
					    static_cast<eng::u32>(m_bob_count));
			if (composed && build_copper()) {
				backend.install_copper_list(m_copper_view);
			}
		}
		eng::u32 fps10 = 0u;
		if (m_fps_win.update(context.frame.frame_index, eng::debug::prof_clock(), fps10)) {
			eng::debug::DebugPeripheral::counter_value(0u, fps10);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// **Fondo estructurado** (regla 25): suelo verde (índice 6), horizonte, tres montañas
	/// (índice 2) con cumbres claras (14), sol naranja (9) y árboles. Se pinta UNA vez.
	void draw_background() {
		eng::u8* const base = m_bitplane_block.view.data();
		// Suelo + horizonte.
		for (eng::u16 y = 169u; y < 256u; ++y) {
			for (eng::u16 x = 0u; x < 320u; ++x) {
				put_bg(base, x, y, 6u);
			}
		}
		for (eng::u16 x = 0u; x < 320u; ++x) {
			put_bg(base, x, 168u, 2u);
		}
		// Sol (disco r=16 en (272, 52), índice 9).
		for (eng::s16 dy = -16; dy <= 16; ++dy) {
			eng::s16 hw = 0;
			while ((hw + 1) * (hw + 1) + dy * dy <= 256) {
				++hw;
			}
			for (eng::s16 dx = -hw; dx <= hw; ++dx) {
				put_bg(base, static_cast<eng::u16>(272 + dx),
				       static_cast<eng::u16>(52 + dy), 9u);
			}
		}
		// Tres montañas (índice 2) con cumbre clara (14).
		const eng::s16 peaks[3][2] = {{52, 116}, {160, 96}, {268, 122}};
		for (eng::u8 m = 0u; m < 3u; ++m) {
			const eng::s16 px = peaks[m][0];
			const eng::s16 py = peaks[m][1];
			for (eng::s16 y = py; y < 169; ++y) {
				const eng::s16 hw = static_cast<eng::s16>((y - py) * 5 / 8);
				for (eng::s16 dx = -hw; dx <= hw; ++dx) {
					const eng::s16 x = static_cast<eng::s16>(px + dx);
					if (x >= 0 && x < 320) {
						put_bg(base, static_cast<eng::u16>(x),
						       static_cast<eng::u16>(y),
						       (y - py) < 4 ? 14u : 2u);
					}
				}
			}
		}
		// Árboles (barras índice 2) sobre el horizonte.
		const eng::u16 tx[5] = {28u, 96u, 172u, 240u, 300u};
		for (eng::u8 i = 0u; i < 5u; ++i) {
			for (eng::u16 y = 152u; y < 182u; ++y) {
				for (eng::u16 x = tx[i]; x < static_cast<eng::u16>(tx[i] + 6u);
				     ++x) {
					put_bg(base, x, y, 2u);
				}
			}
		}
	}

	void build_sprite_sheet(eng::Words<eng::SpriteTag> data) {
		for (eng::u8 inst = 0; inst < kActors; ++inst) {
			for (eng::u8 line = 0; line < kSpriteHeight; ++line) {
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 0u] = 0xFFFFu; // DAT
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 1u] = 0x0000u; // DATB
			}
			// Terminador del canal de DMA: dos palabras a cero tras la DATA.
			const eng::u16 tail = static_cast<eng::u16>(kSpriteHeight) * kWordsPerLine;
			data[static_cast<eng::u16>(inst) * kInstanceWords + tail + 0u] = 0u;
			data[static_cast<eng::u16>(inst) * kInstanceWords + tail + 1u] = 0u;
		}
	}

	/// La aplicación describe los actores; el engine elegirá y podrá reasignar su
	/// representación. Aquí todos son candidatos a sprite (16x16).
	bool add_actors(eng::WordView<eng::SpriteTag> sprite_data) {
		m_dir.clear();
		for (eng::u8 i = 0; i < kActors; ++i) {
			eng::graphics::Visual v {};
			v.pixels = sprite_data.subspan(
				static_cast<eng::u16>(i) * kInstanceWords, kInstanceWords).raw();
			v.w = 16u;
			v.h = kSpriteHeight;
			v.bitplanes = 2u;
			const scene::VisualId visual = m_dir.add_visual(v);
			if (!visual.valid()) {
				return false;
			}
			scene::SpriteDesc sd {};
			sd.content = scene::SpriteVisualRef::simple(visual);
			sd.x = static_cast<eng::s16>(
				static_cast<eng::u16>(kHpos0 + static_cast<eng::u16>(i) * kHposStep) -
				eng::graphics::kSpriteScreenXOrigin);
			sd.y = static_cast<eng::s16>(kY);
			sd.surface = 0u;
			sd.z = static_cast<eng::u8>(10u + i);
			sd.hint.policy = scene::SpritePolicy::Sprite;
			sd.hint.sprite_priority = 0u;
			sd.transparency = scene::TransparencyMode::Opaque;
			sd.background = scene::BackgroundPolicy::None;
			sd.layout = eng::graphics::BobLayout::Interleaved;
			m_ids[i] = m_dir.add(sd);
			if (!m_ids[i].valid()) {
				return false;
			}
		}
		return true;
	}

	/// Compone los sprites del frame (la fachada rellena `placements()`) y vuelca los que caben al
	/// `SpriteManager`.
	bool compose() {
		scene::ActorEmitContext ctx {};
		// sin destino de BOB en esta demo (`ctx.targets` vacío; ver cabecera)
		ctx.cam_x = 0;
		ctx.cam_y = 0;
		ctx.buffer = 0u;
		ctx.display_top = 0x2cu;

		m_frame_plan.clear();
		const scene::SpriteLoadReport res = m_dir.emit(m_frame_plan, ctx);
		m_sprites_in_hw = static_cast<eng::u8>(res.hw);
		m_bob_count = static_cast<eng::u8>(res.degraded);
		return static_cast<eng::u16>(res.hw + res.degraded) == kActors;
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes
		);
		// Reset de los 8 sprites (puntero válido + VSTART/VSTOP=0) ANTES de habilitar
		// SPREN: así el DMA no arma con los registros basura que dejó AmigaDOS.
		const eng::uintptr sprite_base = reinterpret_cast<eng::uintptr>(m_sprite_block.view.data());
		for (eng::u8 c = 0; c < 8u; ++c) {
			sched.move(eng::copper::sprite_pointer_high_register(c), static_cast<eng::u16>(sprite_base >> 16));   // SPRxPTH
			sched.move(eng::copper::sprite_pointer_low_register(c), static_cast<eng::u16>(sprite_base & 0xffffu)); // SPRxPTL
			sched.move(eng::copper::sprite_ctl_register(c), 0x0000); // SPRxCTL (VSTOP=0)
			sched.move(eng::copper::sprite_pos_register(c), 0x0000); // SPRxPOS (VSTART=0)
		}
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane | eng::copper::DmaSprite
			)
		);
		sched.emit_palette(kBasePalette.color);
		// Los sprites se programan DESPUÉS de habilitar SPREN.
		m_dir.present(sched, 0x20u, true);
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_view = sched.chip_view();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	eng::u8  m_sprites_in_hw = 0;
	eng::u8  m_bob_count = 0;
	eng::ChipView<eng::CopperTag> m_copper_view {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	Director m_dir {}; ///< fachada: actores + compose (HW sprites + BOB)
	eng::graphics::FramePlan m_frame_plan {};
	scene::SpriteId m_ids[kActors] {};      ///< ids de los actores (movimiento por frame)
	eng::debug::HudFpsWindow m_fps_win {}; ///< ventana de fps10 (telemetria)
	eng::u32 m_comp_ok = 0u;               ///< composicion coherente (8 HW + 1 degradado)
	eng::u32 m_comp_bad = 0u;              ///< composicion inesperada (deberia ser 0)
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	SpriteAllocatorDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
