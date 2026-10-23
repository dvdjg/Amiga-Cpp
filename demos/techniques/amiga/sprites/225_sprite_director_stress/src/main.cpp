// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/225_sprite_director_stress --keep-running

// ============================================================================
// Demo 225: estrés de la fachada por intención `SpriteDirector`.
// ============================================================================
//
// Mezcla en una sola escena:
//   - dos sprites contiguos (gap de 1 línea: no comparten canal);
//   - un sprite en una franja compartida (canales distintos);
//   - un par *attached* de 15 colores;
//   - una tira horizontal de dos tramos de 16 px;
//   - un sprite compuesto (`CompositeVisual` de una parte);
//   - un BOB forzado (`SpritePolicy::Bob`);
//   - dos `BandPalette` con `top + scroll_y` (playfield + sprites) en la franja baja.
//
// El juego no toca canales, `SPRx*` ni `BlitJob`: registra contenido (`VisualId`/
// `CompositeId`), declara `SpriteHint` y llama `emit`/`present`. El **HUD de depuración
// del host** (`app.debug().text`) muestra nombre + número de demo, contador de frames y
// fps; no consume bitplanes/DMA. La medida real de fps es `measure-fps`.
//
// La copperlist se construye con `copper::SchedulerT`: primero display+paleta estática,
// luego `SpriteDirector::present` (armado/rearmes de sprites) y la banda de playfield
// manualmente en la línea 210+ (por debajo de todos los rearmes).

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/platform/amiga/entry.hpp>

namespace {

namespace scene = eng::scene;

constexpr eng::u16 kBytesPerRow = 40u;
constexpr eng::u8 kPlanes = 4u;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;
constexpr eng::u16 kSpriteH = 16u;
constexpr eng::u16 kSimpleWords = kSpriteH * 2u;   // DAT + DATB por línea
constexpr eng::u16 kAttachedWords = kSpriteH * 4u; // cuatro planos por línea
constexpr eng::u16 kSpriteWords = 512u;
constexpr eng::u16 kCookedWords = 512u;
constexpr eng::u16 kBandTop = 210u;
constexpr eng::u16 kBandHeight = 20u;
constexpr eng::u32 kReadyDetail = 0x00022500u;

constexpr eng::Palette32 kPalette {{
	0x012, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x000, 0xf00, 0x0f0, 0x00f,
	0x000, 0xff0, 0x0ff, 0xf0f,
	0x000, 0xfa0, 0x0fa, 0xaf0,
	0x000, 0xa0f, 0x0af, 0xf0a,
}};

struct DirectorStressDemo {
	void init(auto& app) {
		auto backend = app.device();
		m_bitplane_block = backend.memory_manager().chip().template reserve<eng::PlaneTag>(
			kBitplaneBytes, 16u);
		m_sprite_block = backend.memory_manager().chip().template reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords) * 2u, 16u);
		m_cooked_block = backend.memory_manager().chip().template reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kCookedWords) * 2u, 16u);
		m_copper_block = backend.memory_manager().chip().template reserve<eng::CopperTag>(8192u, 16u);
		if (!m_bitplane_block.valid() || !m_sprite_block.valid() ||
		    !m_cooked_block.valid() || !m_copper_block.valid()) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		m_bitplane_block.view.as_words().fill(0u);
		build_art();
		m_target = eng::graphics::make_bob_target(
			m_bitplane_block.mem_view_chip(), kBytesPerRow, 256u, kPlanes,
			eng::graphics::BobLayout::Planar);
		m_dir.set_cooked_pool(m_cooked_block.mem_view_chip());
		m_dir.set_budget(scene::SpriteBudget {8u, 8u, 60000u, 320u, 32u, 0u, 0u,
						     0xffffu, 0xffffu});
		m_ctx.targets = eng::Span<const eng::graphics::BobTarget> {&m_target, 1u};
		m_ctx.display_top = 0x2cu;
		if (!build_entities() || !build_frame(backend)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Game);
			return;
		}
		if (!backend.takeover_copper(m_copper_view)) {
			eng::amiga::mark_failed(eng::debug::BootFail::Display);
			return;
		}
		eng::debug::DebugPeripheral::counter_name(0u, reinterpret_cast<eng::u32>("fps10"));
		eng::debug::DebugPeripheral::counter_name(1u, reinterpret_cast<eng::u32>("hw"));
		eng::debug::DebugPeripheral::counter_name(2u, reinterpret_cast<eng::u32>("bob"));
		eng::debug::DebugPeripheral::counter_name(3u, reinterpret_cast<eng::u32>("bands"));
		eng::amiga::mark_ready(kReadyDetail);
	}

	void update(auto& app) {
		const eng::u16 t = static_cast<eng::u16>(app.frame());
		m_dir.tick(1u);
		// Movimiento continuo de los sprite simples y del compuesto. Sin multiplicaciones
		// en el bucle: una tabla de seno de 64 entradas y desplazamientos.
		const eng::u8 ph = static_cast<eng::u8>(t & 63u);
		move_simple(m_s0, 20, 40, ph, 18);
		move_simple(m_s1, 44, 56, static_cast<eng::u8>(ph + 8u), 18);
		move_simple(m_s2, 70, 88, static_cast<eng::u8>(ph + 16u), 12);
		move_simple(m_attached, 120, 110, static_cast<eng::u8>(ph + 32u), 10);
		move_simple(m_strip0, 160, 140, static_cast<eng::u8>(ph + 40u), 16);
		move_simple(m_strip1, 176, 140, static_cast<eng::u8>(ph + 40u), 16);
		if (auto a = m_dir.actor(m_bob)) {
			a->desc.x = 280; // BOB: coordenada de pantalla, sin HSTART
			a->desc.y = 60;
		}
		if (auto st = m_dir.composite_state(m_composite)) {
			st->x = static_cast<eng::s16>(220u + eng::graphics::kSpriteScreenXOrigin + (m_sin[static_cast<eng::u8>(t & 63u)] * 8) / 64);
			st->y = static_cast<eng::s16>(150 + (m_sin[static_cast<eng::u8>((t + 21u) & 63u)] * 6) / 64);
		}
		if (auto band = m_dir.band(0u)) {
			band->scroll_y = static_cast<eng::s16>((t & 15u) - 8);
		}
		auto backend = app.device();
		if (build_frame(backend)) {
			backend.commit_copper(m_copper_view);
			++m_frame_counter; // frames RENDERIZADOS (no VBlanks del bucle)
		}
		eng::u32 fps10 = m_host_hud.fps10();
		eng::debug::DebugPeripheral::counter_value(0u, fps10);
		if ((t & 31u) == 0u) {
			eng::debug::DebugPeripheral::counter_value(1u, m_report.hw);
			eng::debug::DebugPeripheral::counter_value(2u, m_report.bob);
			eng::debug::DebugPeripheral::counter_value(3u, m_report.bands);
			m_hud_stats.clear();
			m_hud_stats << "HW" << m_report.hw << " BOB" << m_report.bob
				    << " BAND" << m_report.band_lines;
		}
		m_host_hud.draw(app.debug(), "225 SPRITE DIRECTOR",
				eng::debug::Version {1u, 0u}, m_frame_counter,
				eng::debug::prof_clock(), m_hud_stats.c_str());
	}

	void render(auto& app) { (void)app; }

private:
	static constexpr eng::SineTable<64, 64> m_sin {};
	eng::util::TextLine<64> m_hud_stats {};
	eng::debug::HostHud m_host_hud {};
	eng::u32 m_frame_counter = 0u;

	static void fill_sprite(eng::u16* dst, eng::u16 h, eng::u8 planes, eng::u16 phase,
				eng::u16 color_mask) {
		for (eng::u16 y = 0u; y < h; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			for (eng::u16 x = 0u; x < 16u; ++x) {
				const eng::s16 dx = static_cast<eng::s16>(x) - 7;
				const eng::s16 dy = static_cast<eng::s16>(y) - 7;
				const eng::s16 ax = static_cast<eng::s16>(dx < 0 ? -dx : dx);
				const eng::s16 ay = static_cast<eng::s16>(dy < 0 ? -dy : dy);
				const bool in = static_cast<eng::s16>(ax + ay) <= 7;
				if (!in) continue;
				const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
				const eng::u16 v = static_cast<eng::u16>(1u + ((x + y + phase) & 3u));
				dat = static_cast<eng::u16>(dat | ((v & 1u) ? bit : 0u));
				datb = static_cast<eng::u16>(datb | ((v & 2u) ? bit : 0u));
			}
			if (planes == 2u) {
				dst[y * 2u] = static_cast<eng::u16>(dat & color_mask);
				dst[y * 2u + 1u] = static_cast<eng::u16>(datb & color_mask);
			}
		}
	}

	void build_art() {
		eng::u16* const words = m_sprite_block.view.as_words().data();
		fill_sprite(words, kSpriteH, 2u, 0u, 0xffffu);
		fill_sprite(words + kSimpleWords, kSpriteH, 2u, 1u, 0xffffu);
		fill_sprite(words + kSimpleWords * 2u, kSpriteH, 2u, 2u, 0xffffu);
		fill_sprite(words + kSimpleWords * 3u, kSpriteH, 2u, 3u, 0xffffu);
		// Par attached: cuatro planos con bits alternos (15 colores).
		eng::u16* const att = words + kSimpleWords * 4u;
		for (eng::u16 y = 0u; y < kSpriteH; ++y) {
			att[y * 4u + 0u] = 0xffffu;
			att[y * 4u + 1u] = 0xaaaa;
			att[y * 4u + 2u] = 0x5555u;
			att[y * 4u + 3u] = 0x0000u;
		}
		// Tira: dos tramos de 16 px con arte distinto.
		eng::u16* const strip = words + kSimpleWords * 4u + kAttachedWords;
		fill_sprite(strip, kSpriteH, 2u, 4u, 0xffffu);
		fill_sprite(strip + kSimpleWords, kSpriteH, 2u, 5u, 0xffffu);
		// Compuesto: una parte de 16x8.
		eng::u16* const comp = strip + kSimpleWords * 2u;
		fill_sprite(comp, 8u, 2u, 6u, 0xffffu);
		// BOB planar real (no DAT/DATB de sprite): dos planos contiguos de 16 palabras.
		eng::u16* const bob = comp + kSimpleWords;
		for (eng::u16 y = 0u; y < kSpriteH; ++y) {
			bob[y] = 0xffffu;
			bob[kSpriteH + y] = (y < 8u) ? 0xffffu : 0u;
		}
		// BOB forzado: reutiliza la primera silueta.
		m_visual = eng::graphics::Visual {};
		m_visual_attached = m_visual;
		m_visual_strip0 = m_visual;
		m_visual_strip1 = m_visual;
		m_visual_comp = m_visual;
		m_visual_bob = m_visual;

		m_visual.pixels = eng::Span<const eng::u16> {words, kSimpleWords};
		m_visual.w = 16u;
		m_visual.h = kSpriteH;
		m_visual.bitplanes = 2u;
		m_visual_attached = m_visual;
		m_visual_attached.pixels = eng::Span<const eng::u16> {att, kAttachedWords};
		m_visual_attached.bitplanes = 4u;
		m_visual_strip0 = m_visual;
		m_visual_strip0.pixels = eng::Span<const eng::u16> {strip, kSimpleWords};
		m_visual_strip1 = m_visual;
		m_visual_strip1.pixels = eng::Span<const eng::u16> {strip + kSimpleWords, kSimpleWords};
		m_visual_comp = m_visual;
		m_visual_comp.pixels = eng::Span<const eng::u16> {comp, kSimpleWords};
		m_visual_comp.h = 8u;
		m_visual_bob = m_visual;
		m_visual_bob.pixels = eng::Span<const eng::u16> {bob, kSimpleWords};

		auto& assets = m_dir.assets();
		const auto vs = assets.add_visual(m_visual);
		const auto va = assets.add_visual(m_visual_attached);
		const auto v0 = assets.add_visual(m_visual_strip0);
		const auto v1 = assets.add_visual(m_visual_strip1);
		const auto vc = assets.add_visual(m_visual_comp);
		const auto vb = assets.add_visual(m_visual_bob);
		m_visual_ids[0] = vs;
		m_visual_ids[1] = va;
		m_visual_ids[2] = v0;
		m_visual_ids[3] = v1;
		m_visual_ids[4] = vc;
		m_visual_ids[5] = vb;
	}

	bool build_entities() {
		scene::SpriteHint h {};
		h.policy = scene::SpritePolicy::Sprite;
		scene::SpriteDesc d {};
		d.hint = h;
		d.transparency = scene::TransparencyMode::Opaque;
		d.background = scene::BackgroundPolicy::None;
		d.layout = eng::graphics::BobLayout::Planar;

		d.content = scene::SpriteVisualRef::simple(m_visual_ids[0]);
		d.x = 20;
		d.y = 40;
		m_s0 = m_dir.add(d);
		d.x = 44;
		d.y = 56; // contiguo con s0
		m_s1 = m_dir.add(d);
		d.x = 70;
		d.y = 88;
		m_s2 = m_dir.add(d);
		d.content = scene::SpriteVisualRef::simple(m_visual_ids[1]);
		d.x = 120;
		d.y = 110;
		m_attached = m_dir.add(d);

		d.content = scene::SpriteVisualRef::simple(m_visual_ids[2]);
		d.hint = h;
		d.hint.reuse = scene::SpriteReuse::Horizontal;
		d.hint.strip_id = 7u;
		d.hint.strip_index = 0u;
		d.hint.strip_span = 2u;
		d.x = 160;
		d.y = 140;
		m_strip0 = m_dir.add(d);
		d.content = scene::SpriteVisualRef::simple(m_visual_ids[3]);
		d.hint.strip_index = 1u;
		d.x = 176;
		m_strip1 = m_dir.add(d);

		// Compuesto de una parte.
		scene::SpriteHint hc = h;
		m_part.visual = m_visual_comp;
		m_part.z = 0u;
		m_frame.ticks = 1u;
		m_sequence.frames = eng::Span<const eng::graphics::CompositeFrame> {&m_frame, 1u};
		m_sequence.loop = true;
		m_sequence.next = 0xffu;
		m_composite_visual.parts =
			eng::Span<const eng::graphics::CompositePart> {&m_part, 1u};
		m_composite_visual.sequences =
			eng::Span<const eng::graphics::CompositeSequence> {&m_sequence, 1u};
		const auto cid = m_dir.add_composite(m_composite_visual);
		if (!cid.valid()) {
			return false;
		}
		d.content = scene::SpriteVisualRef::composite_visual(cid);
		d.hint = hc;
		d.x = static_cast<eng::s16>(220u + eng::graphics::kSpriteScreenXOrigin);
		d.y = 150;
		m_composite = m_dir.add(d);

		// BOB forzado.
		d.content = scene::SpriteVisualRef::simple(m_visual_ids[5]);
		d.hint = h;
		d.hint.policy = scene::SpritePolicy::Bob;
		d.x = 280;
		d.y = 60;
		d.sheet_row_bytes = 2u; // hoja planar compacta de 16 px (2 B por fila)
		m_bob = m_dir.add(d);

		if (!m_s0.valid() || !m_s1.valid() || !m_s2.valid() ||
		    !m_attached.valid() || !m_strip0.valid() || !m_strip1.valid() ||
		    !m_composite.valid() || !m_bob.valid()) {
			return false;
		}
		m_playfield[0] = 0xf00u; // rojo
		m_playfield[1] = 0xff0u; // amarillo
		m_sprites[0] = 0x0f0u;   // verde
		m_sprites[1] = 0x00fu;   // azul
		m_band.top = static_cast<eng::s16>(kBandTop);
		m_band.height = kBandHeight;
		m_band.sprites = eng::PaletteWords {m_sprites, 2u};
		m_band.scroll_y = 0;
		m_dir.set_band_palettes(eng::Span<const scene::BandPalette> {&m_band, 1u});
		return true;
	}

	void move_simple(scene::SpriteId id, eng::s16 x, eng::s16 y, eng::u8 phase,
			 eng::s16 amp) {
		if (auto a = m_dir.actor(id)) {
			a->desc.x = static_cast<eng::s16>(x + eng::graphics::kSpriteScreenXOrigin +
							 (m_sin[phase] * amp) / 64);
			a->desc.y = y;
		}
	}

	bool build_frame(auto& backend) {
		eng::copper::SchedulerT<false> sched {m_copper_block};
		sched.emit_planes_display(0x2c81u, 0x2cc1u, 0x0038u, 0x00d0u, kBytesPerRow,
					  0x4200u, kPlanes, m_bitplane_block.mem_view_chip(),
					  kPlaneBytes);
		sched.emit_palette(kPalette.color);
		// `emit_planes_display` enciende Copper+bitplanes; el DMA de sprites se activa
		// explícitamente (sin SPREN, `arm_object` escribe registros pero no hay fetch).
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear |
						 eng::copper::DmaMaster |
						 eng::copper::DmaCopper |
						 eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		m_frame_plan.clear();
		m_report = m_dir.emit(m_frame_plan, m_ctx);
		m_dir.present(sched, 32u);
		// Playfield de la banda, manualmente en el mismo Scheduler (el camino Plan se
		// valida en HOST-485). La línea 210+ queda por debajo de todos los rearmes.
		sched.wait_line_safe(static_cast<eng::u16>(kBandTop + m_band.scroll_y));
		sched.emit_palette(eng::PaletteWords {m_playfield, 2u}, 0u, 2u);
		sched.wait_line(0xf8u);
		sched.move(eng::copper::Register::COLOR00, 0u);
		sched.end();
		m_copper_view = sched.chip_view();
		const bool ok = sched.ok();
		(void)backend.execute_frame_plan(m_frame_plan);
		return ok;
	}

	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	eng::Block<eng::SpriteTag> m_cooked_block {};
	eng::graphics::BobTarget m_target {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::ChipView<eng::CopperTag> m_copper_view {};
	scene::SpriteDirector<8u, 16u, 4u, 8u, 32u, 16u> m_dir {};
	eng::graphics::Visual m_visual {};
	eng::graphics::Visual m_visual_attached {};
	eng::graphics::Visual m_visual_strip0 {};
	eng::graphics::Visual m_visual_strip1 {};
	eng::graphics::Visual m_visual_comp {};
	eng::graphics::Visual m_visual_bob {};
	scene::VisualId m_visual_ids[6] {};
	scene::SpriteId m_s0 {}, m_s1 {}, m_s2 {}, m_attached {}, m_strip0 {},
		m_strip1 {}, m_composite {}, m_bob {};
	eng::graphics::CompositePart m_part {};
	eng::graphics::CompositeFrame m_frame {};
	eng::graphics::CompositeSequence m_sequence {};
	eng::graphics::CompositeVisual m_composite_visual {};
	scene::BandPalette m_band {};
	eng::u16 m_playfield[2] {};
	eng::u16 m_sprites[2] {};
	scene::SpriteLoadReport m_report {};
	scene::ActorEmitContext m_ctx {};
};

} // namespace

int main() {
	eng::GameDisplay display {};
	display.width = 320u;
	display.height = 256u;
	display.color_depth = kPlanes;
	display.buffers = 1u;
	display.palette = kPalette;
	constexpr eng::MemoryConfig memory {128u * 1024u, 8u * 1024u, 4u * 1024u};
	DirectorStressDemo game {};
	return eng::amiga::run_demo(game, display, memory, eng::amiga::VblankWait::Active);
}
