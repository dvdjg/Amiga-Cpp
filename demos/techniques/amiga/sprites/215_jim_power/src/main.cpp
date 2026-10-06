// Lanzar:
//   Depurar   : bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/215_jim_power --debug   && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/215_jim_power --keep-running
//   Optimizada: bash ./tools/build/build-demo.sh demos/techniques/amiga/sprites/215_jim_power --release && bash ./tools/run/run-demo.sh demos/techniques/amiga/sprites/215_jim_power --keep-running

// ============================================================================
// Demo 215: fondo por sprites *Jim Power* — DATA por línea (DMA) + ráfaga de POS.
// ============================================================================
//
// Jim Power pinta el fondo con **2 canales de sprite no attached** (6 y 7) de 4 colores:
// el **DMA** sirve la DATA de cada canal (una fila de 16 px por línea, desde su estructura
// DMA) y el **Copper, por línea, reescribe solo `SPRxPOS`** para repetir el patrón de 32 px
// a lo ancho. Receta (copperlist real, Grok/AHRM «Reusing Sprite DMA Channels»):
//
//   WAIT (vpos, hpos $20)                   ; a la izquierda, tras los slots DMA
//   → el DMA ya carga solo la DATA de la línea (estructura con cabecera)
//   SPR6POS=$40, SPR7POS=$48, SPR6POS=$50, SPR7POS=$58, … ($d8)   ; ráfaga PURA, +$8 (16 px)
//
// Con 2 MOVEs de Copper por período de 16 px entre canales, el Copper debería ir pareado con
// el haz en cuanto arranca ~1 período por delante (primera columna en X 128 con el WAIT en
// X 64); no se ponen WAITs intermedios (colgarían al Copper). **Estado: NO VERIFICADA** —
// con esta ráfaga pura solo se pintan las últimas columnas (el Copper adelanta mucho al haz);
// ver el README de la demo y `consulta-jim-power-data-line-pacing-en.md`.
//
// La **DATA se anima al vuelo**: cada frame se parchean las 160 filas de las dos estructuras
// (fase vertical sobre un patrón de 32 filas), sin regenerar la copperlist.
//
// Encima, un par *attached* de 15 colores (canales 0/1) con `SpriteManager::arm_object`
// (técnica de la 214). Los sprites van delante del playfield (`BPLCON2=0x0024`, AHRM cap. 7).

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

// Display 320x256, 4 planos.
constexpr eng::u16 kBytesPerRow = 40;
constexpr eng::u8  kPlanes = 4;
constexpr eng::u32 kPlaneBytes = static_cast<eng::u32>(kBytesPerRow) * 256u;
constexpr eng::u32 kBitplaneBytes = kPlaneBytes * kPlanes;

// Banda del fondo: líneas 44..203; patrón de 32 px repetido con 20 columnas de 16 px.
constexpr eng::u16 kFirstLine = 44;
constexpr eng::u16 kBandLines = 160;
constexpr eng::u8  kChA = 6;      // primera mitad del patrón (16 px)
constexpr eng::u8  kChB = 7;      // segunda mitad
constexpr eng::u16 kPatRows = 32;
constexpr eng::u8  kArmHpos = 0x20;  // WAIT (hpos = X/2): X=64, antes de la 1.ª columna ($40)
constexpr eng::u16 kFirstPos = 0x40; // 1.ª columna (X=128)
constexpr eng::u16 kColumns = 20;    // $40..$d8 -> X 128..432 (320 px)
constexpr eng::u16 kPosStep = 8;     // +16 px por columna
constexpr eng::u16 kBandCtl = static_cast<eng::u16>((kFirstLine + kBandLines) << 8u);

// Par *attached* de contraste (canales 0/1).
constexpr eng::u16 kHeroHeight = 16;
constexpr eng::u16 kHeroStruct = eng::graphics::attached_pair_structure_words(kHeroHeight); // 36
constexpr eng::u16 kHeroX = 200;
constexpr eng::u16 kHeroY = 120;
constexpr eng::u16 kArmLine = 32;

// Estructuras DMA del fondo: `[POS, CTL, pad, 160 filas DAT/DATB, 0, 0]`. La fila `pad`
// absorbe el primer fetch (la cabecera con `dmastate==1` se lee como DATA en la 1.ª línea).
constexpr eng::u16 kBgStruct = static_cast<eng::u16>(2u + (kBandLines + 1u) * 2u + 2u); // 326
constexpr eng::u16 kHeroWords = static_cast<eng::u16>(kHeroStruct * 2u);                    // 72
constexpr eng::u16 kBgOffA = kHeroWords;
constexpr eng::u16 kBgOffB = static_cast<eng::u16>(kBgOffA + kBgStruct);
constexpr eng::u16 kSpriteWords = static_cast<eng::u16>((kBgOffB + kBgStruct + 3u) & ~3u);

// Paleta: playfield navy; par 6/7 = COLOR29-31 (selva); el par 0/1 *attached* usa COLOR17-31.
constexpr eng::Palette32 kPalette {{
	0x013, 0x222, 0x333, 0x444, 0x555, 0x666, 0x777, 0x888,
	0x999, 0xaaa, 0xbbb, 0xccc, 0xddd, 0xeee, 0xfff, 0x111,
	0x000, 0xf00, 0xf60, 0xfc0, // COLOR16-19
	0xff0, 0x8f0, 0x0f0, 0x0f8, // COLOR20-23
	0x0ff, 0x0af, 0x05f, 0x60f, // COLOR24-27
	0x360, 0x0a2, 0x470, 0x030, // COLOR28-31 (29-31 = verdes del fondo 6/7)
}};

// Patrón de 32 filas x 32 px (4 colores): mitad izquierda (ch6) y derecha (ch7).
struct Pattern {
	eng::u16 rows[kPatRows][4] {}; // dat6, datb6, dat7, datb7
};
constexpr Pattern make_pattern() {
	Pattern p {};
	for (eng::u16 y = 0; y < kPatRows; ++y) {
		eng::u16 dat6 = 0, datb6 = 0, dat7 = 0, datb7 = 0;
		for (eng::u16 x = 0; x < 32u; ++x) {
			const eng::u16 cell = static_cast<eng::u16>(((x >> 2u) ^ (y >> 2u)) & 3u);
			const eng::u16 v = static_cast<eng::u16>((cell == 0u) ? 1u : ((cell == 1u) ? 2u : 3u));
			const eng::u16 bit = static_cast<eng::u16>(0x8000u >> (x & 15u));
			if (x < 16u) {
				if ((v & 1u) != 0u) dat6 = static_cast<eng::u16>(dat6 | bit);
				if ((v & 2u) != 0u) datb6 = static_cast<eng::u16>(datb6 | bit);
			} else {
				if ((v & 1u) != 0u) dat7 = static_cast<eng::u16>(dat7 | bit);
				if ((v & 2u) != 0u) datb7 = static_cast<eng::u16>(datb7 | bit);
			}
		}
		p.rows[y][0] = dat6;
		p.rows[y][1] = datb6;
		p.rows[y][2] = dat7;
		p.rows[y][3] = datb7;
	}
	return p;
}
constexpr Pattern kPattern = make_pattern();

// Arte del par *attached* (canales 0/1): rombo de 16x16 con banda por línea (15 tonos).
struct HeroArt {
	eng::u16 plane[kPlanes][kHeroHeight] {};
};
constexpr HeroArt make_hero() {
	HeroArt a {};
	for (eng::u16 y = 0; y < kHeroHeight; ++y) {
		for (eng::u16 x = 0; x < 16u; ++x) {
			const eng::s32 dx = static_cast<eng::s32>(2u * x + 1u) - 16;
			const eng::s32 dy = static_cast<eng::s32>(2u * y + 1u) - static_cast<eng::s32>(kHeroHeight);
			if (dx * dx + dy * dy > 256) continue;
			const eng::u16 v = static_cast<eng::u16>(1u + (y * 14u) / (kHeroHeight - 1u));
			const eng::u16 bit = static_cast<eng::u16>(0x8000u >> x);
			for (eng::u8 p = 0; p < kPlanes; ++p) {
				if ((v & (1u << p)) != 0u) {
					a.plane[p][y] = static_cast<eng::u16>(a.plane[p][y] | bit);
				}
			}
		}
	}
	return a;
}
constexpr HeroArt kHero = make_hero();

struct JimPowerDemo {
	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({ 128u * 1024u, 8u * 1024u, 4u * 1024u })) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021501u);
			return;
		}
		m_bitplane_block = backend.memory_manager().chip().reserve<eng::PlaneTag>(kBitplaneBytes, 16);
		m_copper_block = backend.memory_manager().chip().reserve<eng::CopperTag>(24576u, 16);
		m_sprite_block = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteWords) * 2u, 16);
		if (!m_bitplane_block.valid() || !m_copper_block.valid() || !m_sprite_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021502u);
			return;
		}

		m_sprites = m_sprite_block.view.as_words().data();
		build_bitplanes(m_bitplane_block.view.as_words().data());
		cook_hero();
		build_bg_structures();
		if (!build_copper()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00021503u);
			return;
		}
		patch_pattern(0u);
		backend.takeover_display(m_copper_ptr);
		eng::debug::mark_ready(g_eng_run_status, 0x00021500u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		patch_pattern(static_cast<eng::u16>(context.frame.frame_index >> 1u));
		if (m_copper_ok) {
			backend.install_copper_list(m_copper_ptr);
		}
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	/// Playfield: navy plano (el fondo de verdad son los sprites).
	void build_bitplanes(eng::u16* words) {
		constexpr eng::u16 kPlaneWords = static_cast<eng::u16>(kPlaneBytes / 2u);
		for (eng::u32 i = 0; i < static_cast<eng::u32>(kPlaneWords) * kPlanes; ++i) {
			words[i] = 0u;
		}
	}

	/// Par *attached* del personaje (helper del engine) en las dos primeras estructuras.
	void cook_hero() {
		const eng::u16* src = &kHero.plane[0][0];
		const eng::usize bytes = static_cast<eng::usize>(kHeroStruct) * 2u;
		const eng::ChipView<eng::SpriteTag> mv = m_sprite_block.mem_view();
		(void)eng::graphics::cook_attached_pair(
			eng::Span<const eng::u16> {src, static_cast<eng::usize>(kPlanes) * kHeroHeight},
			kHeroHeight, 0xfe00u, 0xfe00u, mv.subview(0u, bytes), mv.subview(bytes, bytes));
	}

	/// Estructuras DMA del fondo: cabecera (`POS`/`CTL` de la banda), fila `pad` y las 160
	/// filas de DATA (parcheadas por frame) + terminador.
	void build_bg_structures() {
		for (eng::u8 k = 0; k < 2u; ++k) {
			eng::u16* s = m_sprites + (k == 0u ? kBgOffA : kBgOffB);
			const eng::u16 x_lo = static_cast<eng::u16>(kFirstPos + k * kPosStep);
			s[0] = static_cast<eng::u16>((kFirstLine << 8u) | x_lo); // POS
			s[1] = kBandCtl;                                          // CTL
			for (eng::u16 i = 0; i < static_cast<eng::u16>((kBandLines + 1u) * 2u); ++i) {
				s[2u + i] = 0u; // pad + filas (parcheadas)
			}
			const eng::u16 tail = static_cast<eng::u16>(2u + (kBandLines + 1u) * 2u);
			s[tail + 0u] = 0u;
			s[tail + 1u] = 0u;
		}
	}

	bool build_copper() {
		eng::copper::SchedulerT<false> sched { m_copper_block };
		sched.emit_planes_display(0x2c81, 0x2cc1, 0x0038, 0x00d0, kBytesPerRow, 0x4200,
					  kPlanes, m_bitplane_block.mem_view_chip(), kPlaneBytes);
		sched.move(eng::copper::Register::DMACON,
			   static_cast<eng::u16>(eng::copper::DmaSetClear | eng::copper::DmaMaster |
						 eng::copper::DmaCopper | eng::copper::DmaBitplane |
						 eng::copper::DmaSprite));
		sched.emit_palette(kPalette.color);

		// Personaje *attached* (canales 0/1): armado temprano con el helper del engine.
		sched.wait_line_safe(kArmLine);
		const eng::ChipView<eng::SpriteTag> mv = m_sprite_block.mem_view();
		const eng::usize hero_bytes = static_cast<eng::usize>(kHeroStruct) * 2u;
		eng::graphics::SpriteManager::arm_object(sched, 0u, mv.subview(2u * 2u, hero_bytes - 4u),
							 kHeroX, kHeroY, kHeroHeight, false);
		eng::graphics::SpriteManager::arm_object(sched, 1u,
							 mv.subview(hero_bytes + 2u * 2u, hero_bytes - 4u),
							 kHeroX, kHeroY, kHeroHeight, true);

		// Arranque de la banda (líneas 44..203): PT a la estructura y CTL con el rango, para
		// que el DMA sirva una fila de DATA por línea (dmastate=1 en la banda).
		const eng::uintptr base = reinterpret_cast<eng::uintptr>(m_sprites);
		sched.wait_line_safe(kFirstLine);
		for (eng::u8 k = 0; k < 2u; ++k) {
			const eng::u8 ch = static_cast<eng::u8>(kChA + k);
			const eng::uintptr addr =
				base + static_cast<eng::uintptr>(k == 0u ? kBgOffA : kBgOffB) * 2u;
			sched.move(static_cast<eng::u16>(0x120u + ch * 4u),
				   static_cast<eng::u16>(addr >> 16u));                  // SPRxPTH
			sched.move(static_cast<eng::u16>(0x122u + ch * 4u),
				   static_cast<eng::u16>(addr & 0xffffu));              // SPRxPTL
			sched.move(static_cast<eng::u16>(0x142u + ch * 8u), kBandCtl); // SPRxCTL
			const eng::u16 x_lo = static_cast<eng::u16>(kFirstPos + k * kPosStep);
			sched.move(static_cast<eng::u16>(0x140u + ch * 8u),
				   static_cast<eng::u16>((kFirstLine << 8u) | x_lo));      // SPRxPOS
		}

		// **Ráfaga Jim Power**: por línea, un WAIT a la izquierda (X 64) y una ráfaga PURA de
		// POS alternos ch6/ch7 (+$8 = 16 px) cubriendo 320 px, sin WAITs intermedios.
		for (eng::u16 line = kFirstLine; line < static_cast<eng::u16>(kFirstLine + kBandLines); ++line) {
			sched.wait_position_safe(line, kArmHpos);
			for (eng::u16 k = 0; k < kColumns; ++k) {
				const eng::u8 ch = ((k & 1u) != 0u) ? kChB : kChA;
				const eng::u16 x_lo = static_cast<eng::u16>(kFirstPos + k * kPosStep);
				sched.move(static_cast<eng::u16>(0x140u + ch * 8u),
					   static_cast<eng::u16>((line << 8u) | x_lo));
			}
		}
		sched.wait_line(0xf8);
		sched.end();

		m_copper_ok = sched.ok();
		m_copper_ptr = sched.data();
		return m_copper_ok;
	}

	/// Parchea las filas de DATA de las dos estructuras con la fase del patrón. La fila `i`
	/// se ve en la línea `kFirstLine + 1 + i` (la 1.ª línea de la banda la absorbe el `pad`).
	void patch_pattern(eng::u16 phase) {
		for (eng::u16 i = 0; i < kBandLines; ++i) {
			const eng::u16 row = static_cast<eng::u16>((i + phase) & (kPatRows - 1u));
			const eng::u16 off = static_cast<eng::u16>(2u + (i + 1u) * 2u);
			m_sprites[kBgOffA + off + 0u] = kPattern.rows[row][0]; // DAT (plano 0)
			m_sprites[kBgOffA + off + 1u] = kPattern.rows[row][1]; // DATB (plano 1)
			m_sprites[kBgOffB + off + 0u] = kPattern.rows[row][2];
			m_sprites[kBgOffB + off + 1u] = kPattern.rows[row][3];
		}
	}

	bool m_copper_ok = false;
	const eng::u16* m_copper_ptr = nullptr;
	eng::u16* m_sprites = nullptr;
	eng::Block<eng::PlaneTag> m_bitplane_block {};
	eng::Block<eng::CopperTag> m_copper_block {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_block {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	JimPowerDemo game {};
	eng::Engine engine { backend, game };
	engine.run_frames_polling(0xffff);

	return 0;
}
