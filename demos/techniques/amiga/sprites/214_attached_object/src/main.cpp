// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/214_attached_object --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/214_attached_object --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/214_attached_object --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/214_attached_object --keep-running

// ============================================================================
// Demo 214: sprite *attached* — un objeto de 15 colores (dos canales = un par).
// ============================================================================
//
// Un sprite hardware normal es un canal de 16 px con 3 colores. Al poner el bit
// `ATTACH` (bit 7 de `SPRxCTL`) en el canal **impar** de un par (0+1) y mantenerlo
// en la **misma posición** que su par, ambos canales se combinan en **un solo
// sprite de 16 px con 15 colores**: el par aporta los bits 0-1 del índice y el
// impar los bits 2-3 (índice 0 = transparente; 1..15 = COLOR17..31). Referencia:
// AHRM 3.ª cap. 4 («Attached Sprites», Table 4-5) y
// `docs/reference/amiga/techniques/sprite-layer.md` §4.
//
// Qué muestra:
//   - **Núcleo (par 0/1, 15 colores)**: una gema de 16x24 con una banda de color
//     distinta por línea + brillo especular; dos frames de DATA que alternan
//     (pulso) y rebote X/Y. Solo es posible con *attach*: en 3 colores no hay
//     forma de pintar 15 tonos en un objeto.
//   - **Contraste (canales 2..7, 3 colores)**: seis chispas de 16x16 en sus pares
//     (2/3, 4/5, 6/7), cada par con sus 3 colores, moviéndose por la pantalla.
//
// **Emisión**: cada canal tiene su **estructura DMA** `[POS, CTL, DAT/DATB…, 0,0]`
// en Chip RAM, cocinada con el helper del engine `cook_attached_pair`
// (`eng/graphics/sprite_attached.hpp`) para el par (par = planos 0-1; impar =
// planos 2-3 + ATTACH). El `SPRxPT` debe apuntar a una estructura válida
// (`winuae/sprite-dma.md`). La copperlist se reconstruye por frame y arma los 8
// canales en una línea temprana (32, antes del primer `VSTART`): `WAIT` +
// `SPRxPT`/`SPRxPOS`/`SPRxCTL`, con `SPRxPT` apuntando a la DATA (el canal no
// pinta la cabecera). Los dos canales del par van siempre a la **misma X/Y**;
// separarlos cambia el color de los píxeles (AHRM).
//
// Fondo: 4 planos con bandas de nebulosa + estrellas (white = COLOR15). Los
// sprites van DELANTE del playfield: `BPLCON2=0x0024` de `emit_planes_display`
// (AHRM cap. 7 Table 7-2: `100` = playfields detrás de los sprites).

#include <eng/core/types/span.hpp>
#include <eng/api/api.hpp>
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

// Display 320x256, 4 planos (16 colores de playfield). DIW canonical 0x2c81.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Núcleo *attached*: 16x24, 2 frames. Cada canal del par lleva su estructura DMA.
constexpr eng::u16 kHeroHeight = 24;
constexpr eng::u16 kHeroStruct = eng::graphics::attached_pair_structure_words(kHeroHeight); // 52
constexpr eng::u8  kHeroFrames = 2;

// Chispas de 3 colores: 16x16, un canal cada una (6 = canales 2..7), 2 variantes.
constexpr eng::u8  kSparkles = 6;
constexpr eng::u16 kSparkHeight = 16;
constexpr eng::u16 kSparkStruct = static_cast<eng::u16>(kSparkHeight * 2u + 4u); // 36

// Reparto del bloque de sprites (words): par del núcleo + 2 variantes de chispa.
constexpr eng::u16 kHeroWords = static_cast<eng::u16>(kHeroStruct * 2u); // 104
constexpr eng::u16 kSparkOff = kHeroWords;
constexpr eng::u16 kSparkWords = static_cast<eng::u16>(kSparkStruct * 2u); // 72 (A/B)
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>((kHeroWords + kSparkWords + 3u) & ~3u);

// Posición inicial del núcleo.
constexpr eng::u16 kHeroX0 = 160u;
constexpr eng::u16 kHeroY0 = 48u;

// Línea de armado común: tras el VBlank y antes del primer `VSTART` (mínimo 40 de
// las chispas). El Copper escribe POS/CTL/PT de los 8 canales ahí.
constexpr eng::u16 kArmLine = 32u;

// Cabecera OFF (`VSTART=VSTOP=254`): el canal desarmado lee cabecera válida y no se
// auto-arma con la posición real (antipatrón *Phantom Sprite*, `sprite-layer.md` §10).
constexpr eng::u16 kOffPos = 0xfe00u;
constexpr eng::u16 kOffCtl = 0xfe00u;

// Paleta: 0-7 bandas de nebulosa, 15 estrellas; COLOR16 sin uso (índice 0 del par
// *attached* = transparente); COLOR17-31 = 15 colores de la gema (arcoíris).
constexpr eng::Palette32 kPalette {{
	0x012, 0x023, 0x034, 0x045, 0x256, 0x467, 0x578, 0x68a,
	0x111, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0xfff,
	0x000, 0xf00, 0xf60, 0xfc0, // COLOR16-19 (16 = transparente del par)
	0xff0, 0x8f0, 0x0f0, 0x0f8, // COLOR20-23
	0x0ff, 0x0af, 0x05f, 0x60f, // COLOR24-27 (par 4/5 = 25-27: cian)
	0x90f, 0xf0f, 0xf8f, 0xfff, // COLOR28-31 (par 6/7 = 29-31: magenta/blanco)
}};

// --- Arte del núcleo (4 planos, constexpr: se cocina en COMPILACIÓN) ----------
struct HeroArt {
	eng::u16 plane[kHeroFrames][kPlanes][kHeroHeight] {};
};

/// Elipse de 16x24 con una banda de color por línea (`v` = 1..15) y brillo
/// especular; el frame 2 invierte las bandas (pulso visible).
constexpr HeroArt make_hero_art() {
	HeroArt a {};
	for (eng::u8 f = 0; f < kHeroFrames; ++f) {
		for (eng::u16 y = 0; y < kHeroHeight; ++y) {
			for (eng::u16 x = 0; x < 16u; ++x) {
				// Coordenadas dobladas (sin fracciones): (dx/15)^2 + (dy/24)^2 <= 1.
				const eng::s32 dx = static_cast<eng::s32>(2u * x + 1u) - 16;
				const eng::s32 dy = static_cast<eng::s32>(2u * y + 1u) - static_cast<eng::s32>(kHeroHeight);
				if (dx * dx * 576 + dy * dy * 225 > 129600) {
					continue; // fuera de la elipse: transparente
				}
				eng::u16 v = static_cast<eng::u16>(1u + (y * 14u) / (kHeroHeight - 1u));
				if (f != 0u) {
					v = static_cast<eng::u16>(16u - v); // pulso: bandas invertidas
				}
				// Brillo especular (blanco = COLOR31): se mueve entre frames.
				const eng::s32 hx = static_cast<eng::s32>(x) - ((f == 0u) ? 4 : 5);
				const eng::s32 hy = static_cast<eng::s32>(y) - ((f == 0u) ? 4 : 6);
				if (hx * hx + hy * hy <= 3) {
					v = 15u;
				}
				// `v` = índice de 4 bits -> plano p lleva el bit p (bit 15 = píxel 0).
				const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					if ((v & (1u << p)) != 0u) {
						a.plane[f][p][y] = static_cast<eng::u16>(a.plane[f][p][y] | bit);
					}
				}
			}
		}
	}
	return a;
}
constexpr HeroArt kHeroArt = make_hero_art();

struct AttachedObjectDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021401u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(2048u, 16);
		// Bloque de sprites tipado **Chip** en el tipo: la cocina lo recibe como `ChipView`
		// (`mem_view`) y el banco no se puede olvidar (AGENTS §1.10).
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021402u);
			return;
		}

		m_sprites = m_sprite_block.view.as_words().data();
		cook_hero(0u, kHeroX0, kHeroY0);
		build_sparkle(m_sprite_block.mem_view().subview(kSparkOff * 2u, kSparkStruct * 2u), false);
		build_sparkle(m_sprite_block.mem_view().subview((kSparkOff + kSparkStruct) * 2u,
							       kSparkStruct * 2u),
			      true);
		for (eng::u8 i = 0; i < kSparkles; ++i) {
			m_spark_x[i] = kHeroX0;
			m_spark_y[i] = 96u;
		}
		build_background(m_bitplane_block.view.as_words().data());

		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021403u);
			return;
		}
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00021400u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		const eng::u16 f = context.frame.frame_index;
		// Rebotes triangulares sin * ni / (máscara + resta sobre potencias de 2).
		const eng::u16 tx = tri8(static_cast<eng::u16>(f & 0xffu));
		const eng::u16 ty = tri8(static_cast<eng::u16>((f >> 1u) & 0xffu));
		m_hero_x = static_cast<eng::u16>(160u + tx);        // 160..287
		m_hero_y = static_cast<eng::u16>(48u + (ty >> 1u)); // 48..111
		const eng::u8 hero_frame = static_cast<eng::u8>((f >> 3u) & (kHeroFrames - 1u));
		if (hero_frame != m_hero_frame) {
			// Cambio de frame: se re-cocina la DATA del par (48 words por canal cada 8 frames).
			m_hero_frame = hero_frame;
			cook_hero(m_hero_frame, m_hero_x, m_hero_y);
		}
		for (eng::u8 i = 0; i < kSparkles; ++i) {
			const eng::u16 tx2 = tri8(static_cast<eng::u16>(f + kSparkPhaseX[i]));
			const eng::u16 ty2 = tri8(static_cast<eng::u16>((f >> 2u) + kSparkPhaseY[i]));
			m_spark_x[i] = static_cast<eng::u16>(144u + (tx2 << 1u)); // 144..398
			m_spark_y[i] = static_cast<eng::u16>(48u + ty2);         // 48..175
		}
		if (build_copper()) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Triángulo 0..127..0 desde `t & 0xff` (sin `*`/`/`).
	[[nodiscard]] static constexpr eng::u16 tri8(eng::u16 t) {
		const eng::u16 m = static_cast<eng::u16>(t & 0xffu);
		return (m < 128u) ? m : static_cast<eng::u16>(255u - m);
	}

	/// Cocina la DATA del par *attached* para `frame` (helper del engine) con cabecera OFF
	/// (el Copper arma con POS/CTL en `kArmLine` y `SPRxPT` apunta a la DATA).
	void cook_hero(eng::u8 frame, eng::u16, eng::u16) {
		const eng::u16* src = &kHeroArt.plane[frame][0][0]; // 4 planos contiguos
		const eng::usize bytes = static_cast<eng::usize>(kHeroStruct) * 2u;
		const eng::ChipView<eng::SpriteTag> mv = m_sprite_block.mem_view();
		(void)eng::graphics::cook_attached_pair(
			eng::Span<const eng::u16> {src, static_cast<eng::usize>(kPlanes) * kHeroHeight},
			kHeroHeight, kOffPos, kOffCtl, mv.subview(0u, bytes), mv.subview(bytes, bytes));
	}

	/// Chispa de 3 colores como estructura DMA `[POS, CTL, DAT/DATB, 0,0]` con cabecera
	/// OFF. Rombo de anillos (A) o cruz (B).
	void build_sparkle(eng::ChipView<eng::SpriteTag> dst, bool cross) {
		eng::u16* w = reinterpret_cast<eng::u16*>(dst.address(0).ptr());
		w[0] = kOffPos;
		w[1] = kOffCtl;
		for (eng::u16 y = 0; y < kSparkHeight; ++y) {
			eng::u16 dat = 0u;
			eng::u16 datb = 0u;
			for (eng::u16 x = 0; x < 16u; ++x) {
				const eng::u16 adx = static_cast<eng::u16>(x < 8u ? (8u - x) : (x - 8u));
				const eng::u16 ady = static_cast<eng::u16>(y < 8u ? (8u - y) : (y - 8u));
				const eng::u16 man = static_cast<eng::u16>(adx + ady);
				eng::u16 v = 0u;
				if (cross) {
					// Cruz: brazos por los ejes, núcleo brillante.
					if (adx <= 2u || ady <= 2u) {
						v = (man <= 4u) ? 3u : 2u;
					} else if (man <= 8u) {
						v = 1u;
					}
				} else {
					// Rombo de anillos.
					v = (man <= 6u) ? 3u : ((man <= 13u) ? 2u : ((man <= 20u) ? 1u : 0u));
				}
				if ((v & 1u) != 0u) dat = static_cast<eng::u16>(dat | (0x8000u >> x));
				if ((v & 2u) != 0u) datb = static_cast<eng::u16>(datb | (0x8000u >> x));
			}
			w[2u + y * 2u + 0u] = dat;
			w[2u + y * 2u + 1u] = datb;
		}
		w[kSparkStruct - 2u] = 0u; // terminador de DMA
		w[kSparkStruct - 1u] = 0u;
	}

	/// Fondo: 4 planos con bandas horizontales (0-7) + estrellas blancas.
	void build_background(eng::u16* words) {
		constexpr eng::u16 kWordsPerRow = static_cast<eng::u16>(kBytesPerRow / 2u); // 20
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);   // 5120
		eng::u32 rng = 0x2545f491u;
		for (eng::u16 y = 0; y < 256u; ++y) {
			const eng::u16 band = static_cast<eng::u16>((y >> 5u) & 7u); // 8 bandas de 32 líneas
			for (eng::u16 wx = 0; wx < kWordsPerRow; ++wx) {
				// xorshift32 (sin `*`): ~1 estrella cada 32 words de 16 px.
				rng ^= rng << 13u;
				rng ^= rng >> 17u;
				rng ^= rng << 5u;
				const eng::u16 stars =
					((rng & 0x1fu) == 0u) ? static_cast<eng::u16>(1u << ((rng >> 8u) & 15u)) : 0u;
				for (eng::u8 p = 0; p < kPlanes; ++p) {
					const eng::u16 fill = ((band >> p) & 1u) != 0u ? 0xffffu : 0x0000u;
					words[static_cast<eng::u32>(p) * kPlaneWords + static_cast<eng::u32>(y) * kWordsPerRow + wx] =
						static_cast<eng::u16>(fill | stars);
				}
			}
		}
	}

	/// Copperlist por frame (patrón validado del repo): display, DMACON con `SPREN`, paleta
	/// y, tras un `WAIT` temprano (`kArmLine`, antes de todo `VSTART`), el armado de los 8
	/// canales: `SPRxPT` a la **DATA** (tras la cabecera), `SPRxPOS` y `SPRxCTL` (con
	/// `ATTACH` solo en el canal impar del par). `BPLCON2=0x0024` lo fija
	/// `emit_planes_display` (sprites delante del playfield, AHRM Table 7-2).
	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);

		// Estado del frame: (word_off de la DATA, x, y, alto, attach) por canal.
		struct Arm {
			eng::u16 data_off; // words hasta la PRIMERA DATA (cabecera incluida)
			eng::u16 x, y, height;
			bool attach;
		};
		Arm arms[8] {};
		arms[0] = {0u + 2u, m_hero_x, m_hero_y, kHeroHeight, false};
		arms[1] = {kHeroStruct + 2u, m_hero_x, m_hero_y, kHeroHeight, true};
		for (eng::u8 i = 0; i < kSparkles; ++i) {
			const eng::u16 var = ((i & 1u) != 0u) ? kSparkStruct : 0u;
			arms[2u + i] = {static_cast<eng::u16>(kSparkOff + var + 2u), m_spark_x[i],
					m_spark_y[i], kSparkHeight, false};
		}
		sched.wait_line_safe(kArmLine);
		// Helper del engine (patrón validado; HOST-428): PT a la DATA, POS y CTL (VSTOP
		// exclusivo, ATTACH solo en el impar). La vista debe cubrir DATA+terminador.
		const eng::ChipView<eng::SpriteTag> mv = m_sprite_block.mem_view();
		for (eng::u8 i = 0u; i < 8u; ++i) {
			const eng::usize off = static_cast<eng::usize>(arms[i].data_off) * 2u;
			const eng::usize bytes = static_cast<eng::usize>(arms[i].height) * 4u + 4u;
			eng::graphics::SpriteManager::arm_object(sched, i, mv.subview(off, bytes),
								 arms[i].x, arms[i].y, arms[i].height,
								 arms[i].attach);
		}
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	static constexpr eng::u16 kSparkPhaseX[kSparkles] { 0u, 43u, 85u, 128u, 170u, 213u };
	static constexpr eng::u16 kSparkPhaseY[kSparkles] { 64u, 0u, 96u, 32u, 160u, 128u };

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u16* m_sprites = nullptr; ///< vista mutable del bloque (cocina del núcleo)
	eng::u16 m_hero_x = kHeroX0;
	eng::u16 m_hero_y = kHeroY0;
	eng::u8  m_hero_frame = 0u;
	eng::u16 m_spark_x[kSparkles] {};
	eng::u16 m_spark_y[kSparkles] {};
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	AttachedObjectDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
