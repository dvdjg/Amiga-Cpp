// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/221_sprite_palette_split --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/221_sprite_palette_split --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/221_sprite_palette_split --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/221_sprite_palette_split --keep-running

// ============================================================================
// Demo 221: palette splitting de sprites — el mismo arte, dos paletas por franja.
// ============================================================================
//
// F7/F4.3 de `docs/guides/roadmap/ROADMAP_SPRITES_COMPLETO.md` §3.5. Dos anillos de 16x16
// (valores 1..3 del par de canales 0/1) viven en **un solo canal** (el 0, franjas
// disjuntas) y cada uno declara su **paleta** (`ActorDesc::sprite_palette`). El compositor
// arbitra con `plan_sprite_palettes`: el segundo actor conmuta los registros de su par al
// entrar en su franja (**CopperSwitch**) mediante un `SpritePaletteEvent` que
// `SpriteScene::present` intercala con los rearmes (`emit_placements_into`). Es el mismo
// arte pintado con dos juegos de colores sin gastar planos ni canales extra
// (`sprite-layer.md` §3: los canales de un par comparten COLOR16-19).
//
// La demo usa la **fachada de juego** (`eng::SpriteScene`): alta de actores, `emit`,
// `present`. Los actores suben/bajan juntos (pistón): la conmutación **sigue a la franja**
// del segundo actor porque el evento se emite en su `top` de cada frame.
//
// Fondo: bandas + estrellas (patrón 214/219). COLOR16-19: paleta A (cian) y, en la franja
// del segundo anillo, paleta B (magenta) — ambos arrays indexados por registro.

#include <eng/api/api.hpp>
#include <eng/os/os.hpp>
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
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {
	0xffffffffu, 0, 0, 0, 0, { 0, 0, 0 }
};
}

namespace {

// Display 320x256, 4 planos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Dos anillos 16x16 (2 planos) en Chip; canal 0 (par 0/1 → COLOR16-19).
constexpr eng::u16 kRingWords = 16u * 2u; // DAT/DATB por línea
constexpr eng::u16 kY0[2] = {60u, 140u};  // franjas de los dos anillos (y raster)
constexpr eng::u16 kX = 160u;
constexpr eng::u16 kPiston = 20u;         // recorrido del pistón (px)
constexpr eng::u16 kArmLine = 32u;

// Paleta base (COLOR00-15 fondo/estrellas; COLOR16-19 = paleta A: cian).
constexpr eng::Palette32 kPalette {{
	0x012, 0x023, 0x034, 0x045, 0x256, 0x467, 0x578, 0x68a,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xfff,
	0x000, 0x3cf, 0x8ef, 0xfff, // COLOR16-19 (par 0/1, paleta A)
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
	0x000, 0x000, 0x000, 0x000,
}};

// Paleta B (magenta): array indexado por registro; el arbitraje lee [first..first+count).
eng::u16 g_palette_b[32] {};

struct PaletteSplitDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022101u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(2048u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			(kRingWords + 32u) * 2u, 16);
		m_bob_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(4096u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid() ||
		    !m_bob_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022102u);
			return;
		}

		// Arte del anillo (valores 1..3; bit 0 -> DAT, bit 1 -> DATB).
		eng::u16* const words = m_sprite_block.view.as_words().data();
		for (eng::u16 y = 0u; y < 16u; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			for (eng::u16 x = 0u; x < 16u; ++x) {
				const eng::u16 adx = static_cast<eng::u16>(x < 8u ? (7u - x) : (x - 8u));
				const eng::u16 ady = static_cast<eng::u16>(y < 8u ? (7u - y) : (y - 8u));
				const eng::u16 man = static_cast<eng::u16>(adx + ady);
				eng::u16 v = 0u;
				if (man <= 4u) {
					v = 3u; // núcleo
				} else if (man <= 9u) {
					v = 2u;
				} else if (man <= 13u) {
					v = 1u;
				}
				if ((v & 1u) != 0u) dat = static_cast<eng::u16>(dat | (0x8000u >> x));
				if ((v & 2u) != 0u) datb = static_cast<eng::u16>(datb | (0x8000u >> x));
			}
			words[y * 2u + 0u] = dat;
			words[y * 2u + 1u] = datb;
		}
		m_ring = eng::ChipView<eng::SpriteTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(words), kRingWords * 2u};

		// Paleta B (magenta) indexada por registro.
		for (eng::u16 i = 0u; i < 32u; ++i) {
			g_palette_b[i] = 0x000u;
		}
		g_palette_b[17] = 0xf0fu;
		g_palette_b[18] = 0x90fu;
		g_palette_b[19] = 0xfffu;

		// Escena de juego: dos actores de 16x16 en el canal 0, cada uno con su paleta.
		m_scene.set_budget(eng::scene::RepresentationBudget { 8u, 60000u, 0u });
		eng::graphics::Visual vis {};
		vis.pixels = m_ring.view().as_words().raw();
		vis.w = 16u;
		vis.h = 16u;
		vis.bitplanes = 2u;
		vis.frame_count = 1u;
		for (eng::u8 i = 0u; i < 2u; ++i) {
			eng::scene::ActorDesc d {};
			d.visual = vis;
			d.x = static_cast<eng::s16>(kX);
			d.y = static_cast<eng::s16>(kY0[i]);
			d.preferred_channel = 0u; // un solo canal: franjas disjuntas (reuso vertical)
			d.sprite_palette = (i == 0u) ? kPalette.color : g_palette_b;
			m_ids[i] = m_scene.add(d);
			if (!m_ids[i].valid()) {
				eng::debug::mark_failed(g_eng_run_status, 0x00022103u);
				return;
			}
		}

		build_background(m_bitplane_block.view.as_words().data());
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00022104u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00022100u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		// Pistón: los dos anillos suben/bajan juntos; la conmutación de paleta sigue a la
		// franja del segundo (el evento se emite en su `top` de cada frame).
		if (m_down) {
			m_off = static_cast<eng::u16>(m_off + 1u);
			if (m_off >= kPiston) m_down = false;
		} else {
			m_off = static_cast<eng::u16>(m_off - 1u);
			if (m_off == 0u) m_down = true;
		}
		for (eng::u8 i = 0u; i < 2u; ++i) {
			m_scene.store().at(m_ids[i].index).desc.y =
				static_cast<eng::s16>(kY0[i] + m_off);
		}

		eng::copper::SchedulerT<false> sched { m_copper_block };
		build_display(sched);
		m_frame_plan.clear();
		(void)m_scene.emit(m_frame_plan, m_ctx);
		m_scene.present(sched, kArmLine);
		sched.wait_line(0xf8);
		sched.end();
		if (sched.ok()) {
			m_copper_ptr = sched.data();
			backend.install_copper_list(m_copper_ptr);
			auto& tel = g_eng_frame_telemetry;
			tel.frame = context.frame.frame_index;
			tel.copper_words = sched.words_used();
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	void build_display(eng::copper::SchedulerT<false>& sched) {
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
	}

	bool build_copper() {
		// Contexto de emisión de la fachada (target de BOB por si algo degrada; aquí no).
		m_target = eng::graphics::make_bob_target(m_bob_block.mem_view_chip(), kBytesPerRow,
							  256u,
							  static_cast<eng::u8>(kPlanes),
							  eng::graphics::BobLayout::Planar);
		m_targets[0] = m_target;
		m_ctx.targets = eng::Span<const eng::graphics::BobTarget> {m_targets, 1u};
		m_ctx.display_top = 0x2cu;

		eng::copper::SchedulerT<false> sched { m_copper_block };
		build_display(sched);
		m_frame_plan.clear();
		(void)m_scene.emit(m_frame_plan, m_ctx);
		m_scene.present(sched, kArmLine);
		sched.wait_line(0xf8);
		sched.end();
		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	void build_background(eng::u16* words) {
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u);
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);
		eng::u32 rng = 0x2545f491u;
		for (eng::u16 y = 0u; y < 256u; ++y) {
			const eng::u16 band = static_cast<eng::u16>((y >> 5u) & 7u);
			for (eng::u16 wx = 0u; wx < kWordsPerRow; ++wx) {
				rng ^= rng << 13u;
				rng ^= rng >> 17u;
				rng ^= rng << 5u;
				const eng::u16 stars =
					((rng & 0x1fu) == 0u) ? static_cast<eng::u16>(1u << ((rng >> 8u) & 15u)) : 0u;
				for (eng::u8 p = 0u; p < kPlanes; ++p) {
					const eng::u16 fill = ((band >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
					words[static_cast<eng::u32>(p) * kPlaneWords +
					      static_cast<eng::u32>(y) * kWordsPerRow + wx] =
						static_cast<eng::u16>(fill | stars);
				}
			}
		}
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::ChipView<eng::SpriteTag> m_ring {};
	eng::SpriteScene<8u> m_scene {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::scene::ActorId m_ids[2] {};
	eng::scene::ActorEmitContext m_ctx {};
	eng::graphics::BobTarget m_target {};
	eng::graphics::BobTarget m_targets[1] {};
	eng::u16 m_off = 0u;
	bool m_down = true;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_block {};
	eng::Block<eng::PlaneTag> m_bob_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	PaletteSplitDemo game {};
	eng::Engine engine { backend, game };
	// **Modelo de mensajes** (patrón 216): el latido de VBlank va por la IRQ y el bucle
	// principal duerme en `Wait()` (sin sondear VPOSR).
	(void)eng::os::init(engine, 0u);
	engine.run_frames(0xffff);

	return 0;
}
