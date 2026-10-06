// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/216_attached_actors --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/216_attached_actors --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/216_attached_actors --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/216_attached_actors --keep-running

// ============================================================================
// Demo 216: actores *attached* end-to-end — 15 colores por el camino de actores.
// ============================================================================
//
// Tutorial: el juego da de alta actores cuyo `Visual` declara un **par *attached***
// (`visual.attached = true`, 4 planos de 16 px) en `eng::SpriteScene` y llama `emit`; el
// engine hace el resto:
//   1. reparte **dos canales contiguos** por par con `SpriteAllocator` (par 0+1, 2+3, …);
//   2. cocina las dos **estructuras DMA** del par (canal par = planos 0-1; canal impar =
//      planos 2-3 + `ATTACH`) con `graphics::cook_attached_pair` en el pool Chip que da
//      `set_cooked_pool`, con la DATA del frame vigente de la animación;
//   3. publica **dos** `HwSpritePlacement` por actor (canal par + impar, misma POS).
// El juego solo arma los placements con `SpriteManager::emit_placements_into` (primera config
// de cada canal en una línea temprana y rearmes por franja) y publica con doble buffer de
// copperlist; no escribe `SPRxDATA`/`SPRxPT` ni conoce canales.
//
// Qué muestra: dos gemas de **15 colores** (pares 0/1 y 2/3) que rebotan y animan (2 frames),
// y dos chispas de 3 colores (canales 4/5) que conviven en el mismo reparto. La demo hermana
// `214_attached_object` hace lo mismo **a mano** con el helper; ésta demuestra el camino de
// actores (`ActorDesc` → `compose_sprites` → placements).
//
// Referencias: AHRM 3.ª cap. 4 («Attached Sprites», Table 4-5), `sprite-layer.md` §4 y
// `sprite-techniques-catalog.md` técnica 2. La paleta COLOR16-31 es **compartida** por todos
// los sprites; un par *attached* direcciona COLOR17-31 (WinUAE `drawing.cpp`: `col = v + 16`
// cuando el canal está *attached*), así que las dos gemas lucen los 15 tonos. Las chispas usan
// los 3 tonos de su par no-*attached* (`COLOR25-27`).

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
#include <eng/graphics/copper/double_buffer.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/sprite_attached.hpp>
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

// Display 320x256, 4 planos (16 colores de playfield). DIW canónico 0x2c81.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Actores: 2 gemas *attached* (16x16, 4 planos, 2 frames) + 2 chispas (16x16, 2 planos).
constexpr eng::u8  kGems = 2;
constexpr eng::u8  kSparks = 2;
constexpr eng::usize kActorH = 16u;
constexpr eng::u8  kGemFrames = 2;
constexpr eng::usize kGemFrameWords = 4u * kActorH; // 64 words por frame (4 planos)
constexpr eng::u16 kSparkWords = static_cast<eng::u16>(kActorH * 2u + 2u); // DATA + terminador

// Pool de cocinado del engine: 2 estructuras DMA por par (canal par y canal impar).
constexpr eng::u16 kPairStructWords =
	eng::graphics::attached_pair_structure_words(static_cast<eng::u16>(kActorH)); // 36
constexpr eng::u16 kCookedWords = static_cast<eng::u16>(kGems * 2u * kPairStructWords); // 144
constexpr eng::u16 kSparkSheetWords = static_cast<eng::u16>(kSparks * kSparkWords);

// Línea de armado común: tras el VBlank y antes del primer `VSTART` (mínimo de las gemas).
constexpr eng::u16 kArmLine = 32u;

// Paleta: 0-15 bandas del fondo, 16 = transparente de los pares, 17-31 = 15 tonos del arcoíris.
constexpr eng::Palette32 kPalette {{
	0x012, 0x023, 0x034, 0x045, 0x056, 0x067, 0x078, 0x089,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xfff,
	0x000, 0xf00, 0xf60, 0xfc0, // COLOR16-19 (16 = transparente del par)
	0xff0, 0x8f0, 0x0f0, 0x0f8, // COLOR20-23
	0x0ff, 0x0af, 0x05f, 0x60f, // COLOR24-27
	0x90f, 0xf0f, 0xf8f, 0xfff, // COLOR28-31
}};

// --- Arte de las gemas (4 planos contiguos, constexpr: se cocina en COMPILACIÓN) -----
struct GemArt {
	eng::u16 plane[kGemFrames][4][kActorH] {};
};

/// Rombo de 16x16 con una banda de color por distancia (`v` = 1..15) y brillo móvil; el
/// frame 1 desplaza las bandas (pulso). `v` se escribe como índice de 4 bits: plano `p`
/// lleva el bit `p` (bit 15 = píxel 0, a la izquierda).
constexpr GemArt make_gem_art() {
	GemArt a {};
	for (eng::u8 f = 0; f < kGemFrames; ++f) {
		for (eng::usize y = 0; y < kActorH; ++y) {
			for (eng::usize x = 0; x < kActorH; ++x) {
				const eng::s32 dx = (x < 8u) ? static_cast<eng::s32>(8u - x)
							     : static_cast<eng::s32>(x - 7u);
				const eng::s32 dy = (y < 8u) ? static_cast<eng::s32>(8u - y)
							     : static_cast<eng::s32>(y - 7u);
				const eng::s32 man = dx + dy; // distancia Manhattan al centro
				if (man > 13) {
					continue; // fuera del rombo: transparente
				}
				eng::u16 v = static_cast<eng::u16>(
					1 + ((static_cast<eng::u32>(man) + f * 5u) % 15u));
				// Brillo especular móvil (blanco = COLOR31, índice 15).
				const eng::s32 hx = static_cast<eng::s32>(x) - ((f == 0u) ? 4 : 6);
				const eng::s32 hy = static_cast<eng::s32>(y) - ((f == 0u) ? 4 : 5);
				if (hx * hx + hy * hy <= 2) {
					v = 15u;
				}
				const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
				for (eng::u8 p = 0; p < 4u; ++p) {
					if ((v & (1u << p)) != 0u) {
						a.plane[f][p][y] =
							static_cast<eng::u16>(a.plane[f][p][y] | bit);
					}
				}
			}
		}
	}
	return a;
}
constexpr GemArt kGemArt = make_gem_art();

// Animación compartida por las gemas: 2 frames de 16 ticks (el engine publica la DATA del
// frame vigente). `x` = offset del frame en la hoja (64 px = 64 words de plano contiguo).
constexpr eng::graphics::Frame kGemFramesDef[kGemFrames] = {
	{0u, 0u, 16u, static_cast<eng::u16>(kActorH), 16u, 0u},
	{static_cast<eng::u16>(kGemFrameWords), 0u, 16u, static_cast<eng::u16>(kActorH), 16u, 0u},
};
constexpr eng::graphics::Animation kGemAnim {
	eng::Span<const eng::graphics::Frame> {kGemFramesDef, kGemFrames}, true};

struct AttachedActorsDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021601u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		// **Doble buffer de copperlist** (`copper::DoubleBuffer`): la CPU reemite el bloque
		// inactivo y `flip`+`install` publican; el Copper ejecuta el activo y recarga COP1LC
		// solo al inicio del VBlank. Sin esto, reescribir la lista en ejecución puede partir
		// un WAIT y saltarse los armados de media pantalla (multiplexado).
		if (!m_copper.begin(backend.memory_manager(), 2048u)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021605u);
			return;
		}
		// Hoja de las chispas (DATA de 2 planos + terminador) en Chip: la lee el DMA.
		m_spark_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSparkSheetWords) * 2u, 16);
		// Pool de cocinado de los pares *attached* (lo rellena `compose_sprites`).
		m_cooked_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kCookedWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_spark_block.valid() || !m_cooked_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021602u);
			return;
		}
		build_sparks(m_spark_block.view.as_words());
		build_background(m_bitplane_block.view.as_words().data());
		if (!add_actors(m_spark_block.view.as_words().as_const())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021603u);
			return;
		}
		if (!compose() || !build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021604u);
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
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index); // telemetría de fps
		// Rebote triangular 0..127..0 (sin `*`/`/`); fases distintas por actor. Todas las X
		// caen dentro de la ventana de display (DIWSTRT x≈129): un sprite a la izquierda
		// del borde no se ve (lección de la demo 054).
		m_gem_x[0] = static_cast<eng::u16>(144u + tri8(static_cast<eng::u16>(f << 1u)));
		m_gem_x[1] = static_cast<eng::u16>(176u + tri8(static_cast<eng::u16>((f << 1u) + 96u)));
		// Banda vertical común: los intervalos de los 4 actores se SOLAPAN siempre (tops a
		// ≤15 líneas), así el allocator no reutiliza canales y cada objeto conserva su par
		// de color y su tono (sin *color bleed*). El rearme por multiplexado sí está
		// soportado por `emit_placements_into` (HOST-428) para franjas disjuntas.
		const eng::u16 bounce =
			static_cast<eng::u16>(tri8(static_cast<eng::u16>(f << 1u)) >> 5u); // 0..3
		m_gem_y[0] = static_cast<eng::u16>(102u + bounce);
		m_gem_y[1] = static_cast<eng::u16>(106u + bounce);
		for (eng::u8 i = 0; i < kSparks; ++i) {
			m_spark_y[i] = static_cast<eng::u16>(110u + static_cast<eng::u16>(i) * 4u + bounce);
		}
		// La posición vive en el actor (`ActorDesc`): se actualiza por id y el engine
		// recompone con la nueva franja. Las gemas además avanzan su animación.
		for (eng::u8 i = 0; i < kGems; ++i) {
			auto a = m_scene.store().get(m_ids[i]);
			if (a.valid()) {
				(void)eng::scene::actor_tick(*a, 1u);
				a->desc.x = static_cast<eng::s16>(m_gem_x[i]);
				a->desc.y = static_cast<eng::s16>(m_gem_y[i]);
			}
		}
		for (eng::u8 i = 0; i < kSparks; ++i) {
			auto a = m_scene.store().get(m_ids[kGems + i]);
			if (a.valid()) {
				a->desc.y = static_cast<eng::s16>(m_spark_y[i]);
			}
		}
		if (compose() && build_copper()) {
			m_copper.install(backend); // swap de COP1LC: el Copper estrena la lista en el VBlank
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	[[nodiscard]] static constexpr eng::u16 tri8(eng::u16 t) {
		const eng::u16 m = static_cast<eng::u16>(t & 0xffu);
		return (m < 128u) ? m : static_cast<eng::u16>(255u - m);
	}

	/// Las gemas declaran `visual.attached` (4 planos contiguos); las chispas son sprites de
	/// 2 planos normales. Todas pasan por la misma fachada: el engine decide el reparto.
	bool add_actors(eng::WordView<eng::SpriteTag> spark_sheet) {
		m_scene.clear();
		m_scene.set_budget({8u, 4096u, 0u}); // 8 canales HW, 4k palabras de BOB, 0 capas
		// El pool de cocinado de los pares es del llamador (sin heap): bloque Chip reservado.
		m_scene.set_cooked_pool(m_cooked_block.mem_view());
		for (eng::u8 i = 0; i < kGems; ++i) {
			scene::ActorDesc d {};
			d.visual.kind = eng::graphics::VisualKind::HardwareSprite;
			d.visual.attached = true; // par *attached*: 4 planos -> 15 colores
			d.visual.pixels = eng::Span<const eng::u16> {
				&kGemArt.plane[0][0][0], static_cast<eng::usize>(kGemFrames) * kGemFrameWords};
			d.visual.w = 16u;
			d.visual.h = static_cast<eng::u16>(kActorH);
			d.visual.bitplanes = 4u;
			d.visual.frame_stride = static_cast<eng::u32>(kGemFrameWords) * 2u; // bytes/frame
			d.animation = &kGemAnim;
			d.x = static_cast<eng::s16>(m_gem_x[i]);
			d.y = static_cast<eng::s16>(m_gem_y[i]);
			d.surface = 0u;
			d.z = static_cast<eng::u8>(10u + i);
			d.preferred = scene::Representation::Sprite;
			d.transparency = scene::TransparencyMode::Opaque; // sprite: transparencia nativa
			d.background = scene::BackgroundPolicy::None;
			m_ids[i] = m_scene.add(d);
			if (!m_ids[i].valid()) {
				return false;
			}
		}
		for (eng::u8 i = 0; i < kSparks; ++i) {
			scene::ActorDesc d {};
			d.visual.kind = eng::graphics::VisualKind::HardwareSprite;
			d.visual.pixels = spark_sheet.subspan(
				static_cast<eng::u16>(i) * kSparkWords, kSparkWords).raw();
			d.visual.w = 16u;
			d.visual.h = static_cast<eng::u16>(kActorH);
			d.visual.bitplanes = 2u;
			d.x = static_cast<eng::s16>(144u + static_cast<eng::u16>(i) * 96u);
			d.y = static_cast<eng::s16>(m_spark_y[i]);
			d.surface = 0u;
			d.z = static_cast<eng::u8>(40u + i);
			d.preferred = scene::Representation::Sprite;
			d.transparency = scene::TransparencyMode::Opaque;
			d.background = scene::BackgroundPolicy::None;
			m_ids[kGems + i] = m_scene.add(d);
			if (!m_ids[kGems + i].valid()) {
				return false;
			}
		}
		return true;
	}

	/// Compone el frame con la fachada: los pares *attached* se publican como dos placements
	/// cada uno (y el allocator puede multiplexar canales entre franjas).
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

	/// Copperlist por frame: display, `SPREN`, paleta y **armado de los placements**: la
	/// primera config de cada canal en una línea temprana y los rearmes del multiplexado
	/// vertical en su `vstart` (`SpriteManager::emit_placements_into`). El `SpritesScene`
	/// solo publica el reparto; ni la demo ni el gestor tocan `SPRxPT`.
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper.inactive_block() };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);
		eng::graphics::SpriteManager::emit_placements_into(sched, m_scene.placements(),
								   kArmLine);
		sched.wait_line(0xf8);
		sched.end();

		if (!sched.ok()) {
			return false; // no se publica una lista a medias
		}
		m_copper.flip(); // el bloque recién escrito pasa a activo
		return true;
	}

	/// Chispas de 3 colores: estructura DMA `[DAT/DATB por línea, 0,0]` en Chip (rombo y cruz).
	void build_sparks(eng::Words<eng::SpriteTag> sheet) {
		for (eng::u8 s = 0; s < kSparks; ++s) {
			eng::u16* w = sheet.data() + static_cast<eng::u16>(s) * kSparkWords;
			for (eng::usize y = 0; y < kActorH; ++y) {
				eng::u16 dat = 0u;
				eng::u16 datb = 0u;
				for (eng::usize x = 0; x < kActorH; ++x) {
					const eng::usize adx = (x < 8u) ? (8u - x) : (x - 7u);
					const eng::usize ady = (y < 8u) ? (8u - y) : (y - 7u);
					const eng::usize man = adx + ady;
					eng::u16 v = 0u;
					if (s == 0u) {
						v = (man <= 5u) ? 3u : ((man <= 10u) ? 2u : ((man <= 14u) ? 1u : 0u));
					} else if (adx <= 1u || ady <= 1u) {
						v = (man <= 4u) ? 3u : 2u;
					} else if (man <= 9u) {
						v = 1u;
					}
					if ((v & 1u) != 0u) dat = static_cast<eng::u16>(dat | (0x8000u >> x));
					if ((v & 2u) != 0u) datb = static_cast<eng::u16>(datb | (0x8000u >> x));
				}
				w[y * 2u + 0u] = dat;
				w[y * 2u + 1u] = datb;
			}
			w[kSparkWords - 2u] = 0u; // terminador del canal de DMA
			w[kSparkWords - 1u] = 0u;
		}
	}

	/// Fondo: 4 planos con 16 bandas horizontales + un tablero suave de dos tonos.
	void build_background(eng::u16* words) {
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u); // 20
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);   // 5120
		for (eng::u16 y = 0; y < 256u; ++y) {
			const eng::u16 band = static_cast<eng::u16>((y >> 4u) & 15u); // 16 bandas de 16 líneas
			for (eng::u16 wx = 0; wx < kWordsPerRow; ++wx) {
				// Damero de 8 px en cada banda (dos colores vecinos) para dar textura.
				const eng::u16 v = ((wx ^ (y >> 3u)) & 1u) != 0u
							   ? static_cast<eng::u16>(band | 1u)
							   : band;
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					const eng::u16 fill = ((v >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
					words[static_cast<eng::u32>(p) * kPlaneWords +
					      static_cast<eng::u32>(y) * kWordsPerRow + wx] = fill;
				}
			}
		}
	}

	eng::u16 m_gem_x[kGems] { 144u, 176u };
	eng::u16 m_gem_y[kGems] { 102u, 106u };
	eng::u16 m_spark_y[kSparks] { 110u, 114u };
	scene::ActorId m_ids[kGems + kSparks] {};
	eng::u16 m_sprites_in_hw = 0;
	eng::u8  m_bob_count = 0;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::copper::DoubleBuffer m_copper {}; ///< lista activa/inactiva: sin reescritura en vuelo
	// Bloques **Chip** tipados en el tipo: la chispa la lee el DMA de sprites y el pool lo
	// escribe la cocina; `mem_view()` solo da `ChipView` si el banco está en el tipo (AGENTS §1.10).
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_spark_block {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_cooked_block {};
	eng::SpriteScene<kGems + kSparks> m_scene {}; ///< fachada: actores + compose (pares + BOB)
	eng::graphics::FramePlan m_frame_plan {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	AttachedActorsDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
