// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/217_sprite_template_actor --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/217_sprite_template_actor --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/217_sprite_template_actor --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/217_sprite_template_actor --keep-running

// ============================================================================
// Demo 217: plantilla de franjas por el camino de actores ("chasing the raster").
// ============================================================================
//
// Tutorial: el juego declara **una** `ActorDesc` cuyo `Visual` referencia una
// `HwSpriteTemplate` (bitmap + 3 segmentos + switches de paleta); el engine:
//   1. proyecta las franjas a intents **encadenados** (`sprite_template_to_intents`):
//      todas exigen el MISMO canal y `SpriteAllocator` lo reserva para el rango completo;
//   2. publica **una placement por franja** (mismo canal, `vstart` con gap ≥1);
//   3. entrega la **paleta por franja** como `SpritePaletteEvent` (0-based, línea absoluta);
//   4. `SpriteManager::emit_placements_into` arma la primera franja en una línea temprana y
//      **rearma** el canal en cada franja, intercalando la paleta en orden de línea.
// El juego no escribe `SPRxPT`/`SPRxPOS` ni conoce canales: un solo canal sirve al objeto
// entero (las tres franjas de 24 líneas).
//
// Qué muestra: un "totem" vertical de 3 tramos con formas y colores propios (COLOR17 cambia
// por franja) que se desplaza en horizontal; el canal se rearma y la paleta se conmuta
// mientras el haz baja. Referencia directa (driver, no actores): demo 053.
// Plantilla/segmentos/paleta: `sprite.hpp` (`HwSpriteTemplate`), AHRM 3.ª cap. 4 y
// `sprite-techniques-catalog.md` (técnica 3, multiplexado vertical).

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite_manager.hpp>
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

// Display 320x256, 4 planos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Plantilla: 3 franjas de 16x24 contiguas en el bitmap (DAT/DATB por línea).
constexpr eng::u8  kSegments = 3;
constexpr eng::u8  kSegHeight = 24;
constexpr eng::u16 kSegWords = static_cast<eng::u16>(kSegHeight) * 2u; // 48 words por franja
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kSegWords * kSegments); // 144
constexpr eng::u16 kBaseY = 48u;      // primera línea de la primera franja (raster)
constexpr eng::u16 kArmLine = 32u;    // armado temprano (antes de todo VSTART)
// Rebote X dentro de la ventana de display (DIWSTRT x≈129): un sprite a la izquierda del
// borde no se ve (lección de la demo 054).
constexpr eng::u16 kHpos0 = 144u;     // rebote 144..270 px

// Fondo navy + grises; el cuerpo de cada franja es COLOR17 (lo cambia cada switch).
constexpr eng::Palette32 kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x555, 0xf00, 0x555, 0x555, // COLOR16-19 (COLOR17 = cuerpo, primer tramo)
	0x555, 0x555, 0x555, 0x555, 0x555, 0x555, 0x555, 0x555, 0x555, 0x555, 0x555, 0x555,
}};

// Un tono por franja (COLOR17 de cada tramo).
constexpr eng::u16 kHues[kSegments] = { 0xf00u, 0x0f0u, 0x0ffu };

struct SpriteTemplateActorDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021701u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		// Doble buffer de copperlist: la lista se reemite cada frame (rebote + rearmes).
		if (!m_copper.begin(backend.memory_manager(), 2048u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021705u);
			return;
		}
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021702u);
			return;
		}
		build_sprite_sheet(m_sprite_block.view.as_words());
		build_background(m_bitplane_block.view.as_words().data());
		build_template(m_sprite_block.view.as_words().as_const());
		if (!add_actor()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021703u);
			return;
		}
		if (!compose() || !build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021704u);
			return;
		}
		m_copper.takeover(backend);
		eng::debug::mark_ready(
			g_eng_run_status,
			(static_cast<eng::u32>(m_sprites_in_hw) << 8u) |
				static_cast<eng::u32>(m_bob_count));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u16 f = context.frame.frame_index;
		eng::debug::mark_frame(g_eng_run_status, f); // telemetría de fps
		// Rebote horizontal suave (onda triangular, sin `*`/`/`).
		const eng::u16 period = 64u;
		const eng::u16 phase = static_cast<eng::u16>(f & (2u * period - 1u));
		const eng::u16 t = (phase < period) ? phase
						    : static_cast<eng::u16>(2u * period - 1u - phase);
		m_hpos = static_cast<eng::u16>(kHpos0 + t * 2u);
		// La posición vive en el actor; el engine recompone y rearma cada franja.
		auto a = m_scene.store().get(m_id);
		if (a.valid()) {
			a->desc.x = static_cast<eng::s16>(m_hpos);
		}
		if (compose() && build_copper()) {
			m_copper.install(backend); // swap de COP1LC (estreno en el VBlank)
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Bitmap de la plantilla: 3 franjas contiguas de barras de anchura decreciente
	/// (16, 10, 4 px), cuerpo en DAT (COLOR17) y DATB a 0.
	void build_sprite_sheet(eng::Words<eng::SpriteTag> data) {
		for (eng::u8 seg = 0; seg < kSegments; ++seg) {
			const eng::u8 w = static_cast<eng::u8>(16u - static_cast<eng::u8>(seg) * 6u);
			const eng::u8 margin = static_cast<eng::u8>(16u - w);
			const eng::u16 body =
				static_cast<eng::u16>((0xFFFFu >> margin) << (margin / 2u));
			for (eng::u8 line = 0; line < kSegHeight; ++line) {
				data[static_cast<eng::u16>(seg) * kSegWords + line * 2u + 0u] = body;
				data[static_cast<eng::u16>(seg) * kSegWords + line * 2u + 1u] = 0x0000u;
			}
		}
	}

	/// Plantilla de capacidad fija (miembro, vida estable): segmentos contiguos y un
	/// switch de COLOR17 por franja, con la línea **relativa** al top del actor.
	void build_template(eng::WordView<eng::SpriteTag> sheet) {
		m_template.width_words = 1u;
		m_template.bitmap = sheet.raw();
		for (eng::u8 i = 0; i < kSegments; ++i) {
			m_template.segments[i] = {
				static_cast<eng::u16>(i * kSegWords),
				kSegHeight,
				static_cast<eng::u16>(i * kSegHeight),
			};
			m_template.switches[i] = {
				static_cast<eng::u16>(i * (kSegHeight + 1u)), &kHues[i], 17u, 1u};
		}
		m_template.segment_count = kSegments;
		m_template.switch_count = kSegments;
	}

	/// El juego describe UN actor con la plantilla; el engine decide cómo materializarlo.
	bool add_actor() {
		m_scene.clear();
		m_scene.set_budget({8u, 4096u, 0u});
		scene::ActorDesc d {};
		d.visual.kind = eng::graphics::VisualKind::HardwareSprite;
		// Contenido para el fallback a BOB (la primera franja como sprite normal).
		d.visual.pixels = m_sprite_block.view.as_words().as_const().subspan(0u, kSegWords).raw();
		d.visual.w = 16u;
		d.visual.h = kSegHeight;
		d.visual.bitplanes = 2u;
		d.sprite_template = m_template.view(); // franjas + rearme + paleta
		d.x = static_cast<eng::s16>(m_hpos);
		d.y = static_cast<eng::s16>(kBaseY);
		d.z = 10u;
		d.preferred = scene::Representation::Sprite;
		d.transparency = scene::TransparencyMode::Opaque; // sprite: transparencia nativa
		d.background = scene::BackgroundPolicy::None;
		m_id = m_scene.add(d);
		return m_id.valid();
	}

	/// Compone: la plantilla produce una placement por franja (mismo canal) + eventos de
	/// paleta; la fachada los deja en `placements()`/`palette_events()`.
	bool compose() {
		scene::ActorEmitContext ctx {};
		ctx.cam_x = 0;
		ctx.cam_y = 0;
		ctx.buffer = 0u;
		ctx.display_top = 0x2cu;

		m_frame_plan.clear();
		const scene::SpriteComposeResult res = m_scene.emit(m_frame_plan, ctx);
		m_sprites_in_hw = static_cast<eng::u8>(res.sprites);
		m_bob_count = static_cast<eng::u8>(res.degraded);
		return res.ok;
	}

	/// Copperlist: display, `SPREN`, paleta base y armado de los placements con la paleta
	/// por franja intercalada (`emit_placements_into`).
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper.inactive_block() };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kBasePalette.color);
		eng::graphics::SpriteManager::emit_placements_into(
			sched, m_scene.placements(), kArmLine, m_scene.palette_events());
		sched.wait_line(0xf8);
		sched.end();

		if (!sched.ok()) {
			return false; // no se publica una lista a medias
		}
		m_copper.flip();
		return true;
	}

	/// Fondo navy con bandas horizontales suaves (contexto del sprite).
	void build_background(eng::u16* words) {
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u); // 20
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);   // 5120
		for (eng::u16 y = 0; y < 256u; ++y) {
			const eng::u16 band = static_cast<eng::u16>((y >> 5u) & 7u); // 8 bandas de 32
			for (eng::u16 wx = 0; wx < kWordsPerRow; ++wx) {
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					const eng::u16 fill = ((band >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
					words[static_cast<eng::u32>(p) * kPlaneWords +
					      static_cast<eng::u32>(y) * kWordsPerRow + wx] = fill;
				}
			}
		}
	}

	eng::u16 m_hpos = kHpos0;
	eng::u16 m_sprites_in_hw = 0;
	eng::u8  m_bob_count = 0;
	scene::ActorId m_id {};
	eng::graphics::HwSpriteTemplate<kSegments, kSegments> m_template {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::copper::DoubleBuffer m_copper {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	eng::SpriteScene<1> m_scene {}; ///< fachada: un actor con plantilla de franjas
	eng::graphics::FramePlan m_frame_plan {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	SpriteTemplateActorDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
