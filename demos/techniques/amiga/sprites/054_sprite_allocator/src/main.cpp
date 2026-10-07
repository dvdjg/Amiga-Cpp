// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/054_sprite_allocator --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/054_sprite_allocator --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/054_sprite_allocator --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/054_sprite_allocator --keep-running

// ============================================================================
// Demo 054: sistema de objetos — composición de sprites y overflow a BOB.
// ============================================================================
//
// Consume la capa de objetos del engine (`docs/engine/architecture/OBJECT_SYSTEM.md`):
// la escena describe ACTORES (`ActorDesc`), y `compose_sprites` hace el trabajo:
//
//   1. ordena los actores por superficie y `z`;
//   2. construye una `SpriteIntent` por actor (ordenada por `top`);
//   3. reparte canales con el `SpriteAllocator` (multiplexado vertical);
//   4. publica los `HwSpritePlacement` que caben, que el `SpriteManager` materializa
//      en registros (`SPRxPOS/CTL/PT`) dentro de la copperlist;
//   5. cuenta los que no caben (`as_bob`).
//
// Qué muestra: 9 actores de 16×16 en una fila horizontal (y=100). Los 8 primeros caben
// en los canales 0..7 (parejas de color: rojo, verde, azul, amarillo); el noveno no cabe
// y queda `as_bob`. El reparto se publica en `g_eng_run_status.detail`
// (`sprites << 8 | degradados`).
//
// NOTA DE CONTENIDO: un sprite hardware lee su DATA como DAT/DATB por línea, mientras
// que un BOB planar lee planos contiguos con máscara. El `Visual` tiene una sola vista
// de píxeles, así que degrado a BOB se **cuenta** pero no se dibuja: materializarlo pide
// un asset planar cocinado aparte (o un layout canónico que sirva a ambos caminos). El
// destino de BOB se declara como `nullptr`, que produce el rechazo controlado de ese
// camino sin afectar al resto.
//
// Fondo EHB estático (6 planos) con COLOR00 navy.

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
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

constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 6;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// 9 actores de 16x16 en fila dentro de la ventana (x = 144, 176, ..., 400); solo 8 caben.
constexpr eng::u8  kActors = 9;
constexpr eng::u8  kSpriteHeight = 16;
constexpr eng::u16 kWordsPerLine = 2;   // DAT + DATB
// **Animación del bitmap (§7)**: cada instancia lleva DOS frames (16 líneas x DAT/DATB)
// y, detrás, DOS palabras a cero que terminan el canal de DMA (AHRM 3.ª: "two all-zero
// words are placed at the end of the data structure to stop the DMA channel"). Sin ellas el
// canal sigue leyendo la instancia vecina. `frame_stride` separa los frames (bytes).
constexpr eng::u8  kFrames = 2;
constexpr eng::u16 kFrameWords = static_cast<eng::u16>(kSpriteHeight) * kWordsPerLine; // 32
constexpr eng::u32 kFrameStride = static_cast<eng::u32>(kFrameWords) * 2u;             // 64 B
constexpr eng::u16 kInstanceWords = static_cast<eng::u16>(kFrameWords * kFrames) + 2u; // 66
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>(kInstanceWords * kActors);              // 594
constexpr eng::u16 kY = 100;
constexpr eng::u16 kHpos0 = 144;
constexpr eng::u16 kHposStep = 32;

// Animación compartida de 2 frames (16 ticks cada uno): el camino de sprite publica la DATA
// del frame vigente (`SPRITE_CHANNEL_WINDOWS` §7), así que el sprite cambia de imagen solo.
constexpr eng::graphics::Frame kActorFrames[kFrames] = {
	{0u, 0u, 16u, static_cast<eng::u16>(kSpriteHeight), 16u, 0u},
	{0u, 0u, 16u, static_cast<eng::u16>(kSpriteHeight), 16u, 0u},
};
constexpr eng::graphics::Animation kActorAnim {
	eng::Span<const eng::graphics::Frame> {kActorFrames, kFrames}, true};

// Fondo navy + parejas de color de sprite: COLOR17=rojo, COLOR21=verde,
// COLOR25=azul, COLOR29=amarillo (cada par de canales comparte su gama).
constexpr eng::Palette32 kBasePalette {{
	0x013, 0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777,
	0x888, 0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff,
	0x111, 0xf00, 0x111, 0x111,   // COLOR16-19
	0x222, 0x0f0, 0x222, 0x222,   // COLOR20-23
	0x333, 0x00f, 0x333, 0x333,   // COLOR24-27
	0x444, 0xff0, 0x444, 0x444,   // COLOR28-31
}};

struct SpriteAllocatorDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
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

		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(
			g_eng_run_status,
			(static_cast<eng::u32>(m_sprites_in_hw) << 8u) |
				static_cast<eng::u32>(m_bob_count)
		);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		// §7: avanza la animación (el sprite publica la DATA del frame vigente) y recompone
		// el frame (reparto + emisión). La copperlist se reconstruye porque cambia la DATA.
		for (eng::u16 i = 0u; i < m_actors.count(); ++i) {
			auto a = m_actors.get(m_actors.id_at(i));
			if (a.valid()) {
				(void)eng::scene::actor_tick(*a, 1u);
			}
		}
		if (m_copper_ok && compose() && build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_sprite_sheet(eng::Words<eng::SpriteTag> data) {
		for (eng::u16 inst = 0; inst < kActors; ++inst) {
			const eng::u16 base = static_cast<eng::u16>(inst) * kInstanceWords;
			for (eng::u8 f = 0; f < kFrames; ++f) {
				for (eng::u8 line = 0; line < kSpriteHeight; ++line) {
					// Frame 0: barra de 16 px; frame 1: barra central de 8 px (cambio visible).
					const eng::u16 dat = (f == 0u) ? 0xFFFFu : 0x0FF0u;
					data[base + f * kFrameWords + line * 2u + 0u] = dat; // DAT
					data[base + f * kFrameWords + line * 2u + 1u] = 0u;  // DATB
				}
			}
			// Terminador del canal de DMA: dos palabras a cero tras el ÚLTIMO frame.
			const eng::u16 tail = static_cast<eng::u16>(kFrameWords * kFrames);
			data[base + tail + 0u] = 0u;
			data[base + tail + 1u] = 0u;
		}
	}

	/// La aplicación describe los actores; el engine elegirá y podrá reasignar su
	/// representación. Aquí todos son candidatos a sprite (16x16).
	bool add_actors(eng::WordView<eng::SpriteTag> sprite_data) {
		m_actors.reset();
		m_allocator.reset({8u, 4096u, 0u});
		for (eng::u8 i = 0; i < kActors; ++i) {
			scene::ActorDesc d {};
			d.visual.pixels = sprite_data.subspan(
				static_cast<eng::u16>(i) * kInstanceWords, kInstanceWords).raw();
			d.visual.w = 16u;
			d.visual.h = kSpriteHeight;
			d.visual.bitplanes = 2u;
			// §7: la hoja tiene 2 frames (stride en bytes) y el actor avanza la animación.
			d.visual.frame_stride = kFrameStride;
			d.animation = &kActorAnim;
			d.x = static_cast<eng::s16>(kHpos0 + static_cast<eng::u16>(i) * kHposStep);
			d.y = static_cast<eng::s16>(kY);
			d.surface = 0u;
			d.z = static_cast<eng::u8>(10u + i);
			d.preferred = scene::Representation::Sprite;
			d.transparency = scene::TransparencyMode::Opaque; // sprite: transparencia nativa
			d.background = scene::BackgroundPolicy::None;
			d.layout = eng::graphics::BobLayout::Interleaved;
			if (!m_actors.add(d, m_allocator).valid()) {
				return false;
			}
		}
		return true;
	}

	/// Compone los sprites del frame y vuelca los que caben al `SpriteManager`.
	bool compose() {
		scene::ActorEmitContext ctx {};
		// sin destino de BOB en esta demo (`ctx.targets` vacío; ver cabecera)
		ctx.cam_x = 0;
		ctx.cam_y = 0;
		ctx.buffer = 0u;
		ctx.display_top = 0x2cu;

		scene::SpriteComposeScratch sc {};
		sc.order = m_order;
		sc.intents = m_intents;
		sc.intent_actor = m_intent_actor;
		sc.slots = m_slots;
		sc.placements = m_placements;

		m_frame_plan.clear();
		const scene::SpriteComposeResult res =
			scene::compose_sprites(m_frame_plan, m_actors, ctx, 0x2cu, sc);
		m_sprites_in_hw = static_cast<eng::u8>(res.sprites);
		m_bob_count = static_cast<eng::u8>(res.degraded);
		const eng::u8 applied = m_sprites.apply(m_placements, m_sprites_in_hw);
		return applied == m_sprites_in_hw;
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
			sched.move(static_cast<eng::u16>(0x120u + c * 4u), static_cast<eng::u16>(sprite_base >> 16));   // SPRxPTH
			sched.move(static_cast<eng::u16>(0x122u + c * 4u), static_cast<eng::u16>(sprite_base & 0xffffu)); // SPRxPTL
			sched.move(static_cast<eng::u16>(0x142u + c * 8u), 0x0000); // SPRxCTL (VSTOP=0)
			sched.move(static_cast<eng::u16>(0x140u + c * 8u), 0x0000); // SPRxPOS (VSTART=0)
		}
		sched.move(
			eng::copper::Register::DMACON,
			static_cast<eng::u16>(
				eng::copper::DmaSetClear | eng::copper::DmaMaster | eng::copper::DmaCopper |
				eng::copper::DmaBitplane | eng::copper::DmaSprite
			)
		);
		sched.emit_palette(kBasePalette.color);
		// Los sprites se programan DESPUÉS de habilitar SPREN, con el patrón de objetos
		// validado (HOST-428): un solo WAIT temprano (línea 32, antes del VSTART de los
		// actores) y el armado de los 8 canales con PT/POS/CTL (VSTOP exclusivo, ATTACH).
		m_sprites.emit_armed_into(sched, 32u);
		sched.wait_line(0xf8);
		sched.move(eng::copper::Register::COLOR00, 0x0000);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_words = sched.words_used();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	bool m_copper_ok = false;
	eng::u16 m_copper_words = 0;
	eng::u8  m_sprites_in_hw = 0;
	eng::u8  m_bob_count = 0;
	const eng::u16* m_copper_ptr = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag> m_sprite_block {};
	scene::ActorStore<kActors> m_actors {};
	scene::RepresentationAllocator m_allocator {};
	scene::ActorId m_order[kActors] {};
	eng::graphics::SpriteIntent m_intents[kActors] {};
	eng::u16 m_intent_actor[kActors] {};
	eng::graphics::SpriteSlot m_slots[kActors] {};
	eng::graphics::HwSpritePlacement m_placements[kActors] {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::graphics::SpriteManager m_sprites {};
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
