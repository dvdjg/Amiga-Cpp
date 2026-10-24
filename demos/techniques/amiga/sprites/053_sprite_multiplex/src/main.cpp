// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/053_sprite_multiplex --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/053_sprite_multiplex --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/053_sprite_multiplex --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/053_sprite_multiplex --keep-running

// ============================================================================
// Demo 053: multiplexado de sprites hardware + color multiplexing.
// ============================================================================
//
// Valida el camino de sprites de la nueva estructura del engine a través de
// `scene::SpriteDirector`: una plantilla portable (`HwSpriteTemplate`) con seis
// segmentos y cambios de paleta, publicada con `emit` + `present`.
//
// Qué muestra: SEIS objetos de 16 px de ancho y altura 24, con anchura creciente
// (pirámide), servidos por UN SOLO canal de sprite hardware (canal 0). Cada
// objeto se rearma en su línea (el Copper reapunta SPRxPT/POS/CTL) y cambia su
// color `COLOR17` en esa misma línea. Es la técnica de amiga-bootcamp: el juego
// emite una entidad con plantilla; el director materializa el hardware.
//
// Fondo EHB estático (6 planos) con COLOR00 navy para que los sprites destaquen.

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite.hpp>
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

// Geometría del display EHB 320x256 (igual que StaticEhbScene).
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u; // 10240
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;                    // 61440

// Sprites: 6 instancias de 16x24 (1 word de ancho, DAT+DATB intercalados).
constexpr eng::u8  kInstances = 6;
constexpr eng::u8  kSpriteHeight = 24;
constexpr eng::u16 kWordsPerLine = 2;   // DAT + DATB
constexpr eng::u16 kInstanceWords = static_cast<eng::u16>(kSpriteHeight) * kWordsPerLine; // 48
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kInstanceWords * kInstances);     // 288
constexpr eng::u32 kFailAddActor = 0x00005304u;
constexpr eng::u16 kBaseY = 40;         // primera línea del primer segmento
constexpr eng::u16 kSpriteHpos = 152;   // centrado: (320-16)/2

// Fondo navy (COLOR00) + grises. COLOR17 (cuerpo del sprite) se sobreescribe por
// instancia con los `HwSpritePaletteSwitch`.
constexpr eng::Palette32 kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x555, 0x555, 0x555, 0x555,
	0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333, 0x333,
}};

// Seis tonos saturados (RGB444): uno por instancia.
constexpr eng::u16 kHues[kInstances] = { 0xf00, 0x0f0, 0x00f, 0xff0, 0x0ff, 0xf0f };

// 6 segmentos -> SpriteScene<2> reserva 8 intents (2 * 4) para una entidad.
using Director = eng::scene::SpriteDirector<2u, 1u, 1u, 1u, 8u, 8u>;

struct SpriteMultiplexDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		m_memory_ok = backend.configure_memory({
			96u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});
		if (!m_memory_ok) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005301u);
			return;
		}

		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(1024, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005302u);
			return;
		}
		eng::Words<eng::SpriteTag> sprite_data = m_sprite_block.view.as_words();

		build_sprite_sheet(sprite_data);
		build_template(sprite_data.as_const());
		if (!add_actor(sprite_data.as_const())) {
			eng::debug::mark_failed(g_eng_run_status, kFailAddActor);
			return;
		}

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00005303u);
			return;
		}

		backend.takeover_display(m_copper_view);
		eng::debug::mark_ready(g_eng_run_status, static_cast<eng::u32>(m_copper_words));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		// Rebote horizontal suave (onda triangular): los seis sprites se desplazan en
		// bloque, demostrando el reposicionado por Copper sin tocar los bitplanes.
		const eng::u16 frame = context.frame.frame_index;
		const eng::u16 period = 64u;
		const eng::u16 phase = static_cast<eng::u16>(frame & (2u * period - 1u));
		const eng::u16 t = (phase < period) ? phase : static_cast<eng::u16>(2u * period - 1u - phase);
		m_hpos = static_cast<eng::u16>(40u + t * 2u);  // 40..166 px de pantalla
		if (m_dir.actor(m_id).valid()) {
			m_dir.actor(m_id)->desc.x = static_cast<eng::s16>(m_hpos + eng::graphics::kSpriteScreenXOrigin);
		}
		if (build_copper()) {
			backend.install_copper_list(m_copper_view);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Hoja de sprites: 6 barras centradas de anchura decreciente (16..6 px), cada
	/// una de 24 líneas. El cuerpo usa DAT=1 (COLOR17) y DATB=0.
	void build_sprite_sheet(eng::Words<eng::SpriteTag> data) {
		for (eng::u8 inst = 0; inst < kInstances; ++inst) {
			const eng::u8 w = static_cast<eng::u8>(16u - static_cast<eng::u8>(inst) * 2u);
			const eng::u8 margin = static_cast<eng::u8>(16u - w);
			const eng::u16 body = static_cast<eng::u16>(
				(0xFFFFu >> margin) << (margin / 2u)
			);
			for (eng::u8 line = 0; line < kSpriteHeight; ++line) {
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 0u] = body;  // DAT
				data[static_cast<eng::u16>(inst) * kInstanceWords + line * 2u + 1u] = 0x0000u; // DATB
			}
		}
	}

	/// Plantilla: 6 segmentos (uno por instancia, reuso vertical del canal) y 6
	/// cambios de paleta (COLOR17 = tono de cada instancia).
	void build_template(eng::WordView<eng::SpriteTag> sprite_data) {
		m_template.width_words = 1;
		m_template.bitmap = sprite_data.raw();
		for (eng::u8 i = 0; i < kInstances; ++i) {
			m_template.segments[i] = {
				static_cast<eng::u16>(i * kInstanceWords),
				kSpriteHeight,
				static_cast<eng::u16>(i * kSpriteHeight),
			};
			const eng::u16 line = static_cast<eng::u16>(kBaseY + i * (kSpriteHeight + 1u));
			// `PaletteWords` es una vista tipada (word view): puntero + tamaño.
			m_template.switches[i] = {line, eng::PaletteWords {&kHues[i], 1u}, 17u};
		}
		m_template.segment_count = kInstances;
		m_template.switch_count = kInstances;
	}

	bool add_actor(eng::WordView<eng::SpriteTag> sheet) {
		eng::graphics::Visual v {};
		v.pixels = sheet.subspan(0u, kInstanceWords).raw();
		v.w = 16u;
		v.h = kSpriteHeight;
		v.bitplanes = 2u;
		const scene::VisualId visual = m_dir.add_visual(v);
		if (!visual.valid()) {
			return false;
		}
		scene::SpriteDesc d {};
		d.content = scene::SpriteVisualRef::simple(visual);
		d.sprite_template = m_template.view();
		d.hint.policy = scene::SpritePolicy::Sprite;
		d.hint.reuse = scene::SpriteReuse::VerticalDma;
		d.x = static_cast<eng::s16>(m_hpos);
		d.y = static_cast<eng::s16>(kBaseY);
		d.z = 10u;
		d.transparency = scene::TransparencyMode::Opaque;
		d.background = scene::BackgroundPolicy::None;
		m_id = m_dir.add(d);
		return m_id.valid();
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(
			0x2c81, 0x2cc1, 0x0038, 0x00d0,
			kBytesPerRow, 0x6200, kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes
		);
		// Reset del sprite 0 (VSTART/VSTOP=0 + puntero a datos válidos) mientras su
		// DMA todavía está limpio (`emit_planes_display` lo apagó): evita que, al
		// habilitar SPREN abajo, el sprite arme con los registros basura de AmigaDOS
		// y haga una lectura DMA de un puntero inválido.
		const eng::u32 sprite_addr = reinterpret_cast<eng::u32>(m_sprite_block.view.data());
		sched.move(eng::copper::sprite_pointer_high_register(0u), static_cast<eng::u16>(sprite_addr >> 16));   // SPR0PTH
		sched.move(eng::copper::sprite_pointer_low_register(0u), static_cast<eng::u16>(sprite_addr & 0xffff)); // SPR0PTL
		sched.move(eng::copper::sprite_ctl_register(0u), 0x0000); // SPR0CTL (VSTOP=0)
		sched.move(eng::copper::sprite_pos_register(0u), 0x0000); // SPR0POS (VSTART=0)
		// `emit_planes_display` no activa el DMA de sprites: lo habilitamos aquí.
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane | eng::copper::DmaSprite
			)
		);
		sched.emit_palette(kBasePalette.color);
		m_frame_plan.clear();
		scene::ActorEmitContext ctx {};
		ctx.display_top = 0x2cu;
		m_report = m_dir.emit(m_frame_plan, ctx);
		m_dir.present(sched, 0x20u, true);
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_view = sched.chip_view();
		return m_copper_ok;
	}

	bool m_memory_ok = false;
	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	eng::ChipView<eng::CopperTag> m_copper_view {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	eng::graphics::HwSpriteTemplate<kInstances, kInstances> m_template {};
	Director m_dir {};
	scene::SpriteId m_id {};
	scene::SpriteLoadReport m_report {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::u16 m_hpos = kSpriteHpos;   // posición horizontal animada en update()
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	SpriteMultiplexDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
