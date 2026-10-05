// Demo - PORT de Donkey Kong (NES) sobre el engine: decompilado nativo + BG por BLOQUES de tiles
// con el BLITTER (metatiles 16x16), no copia pixel a pixel por CPU.
//
// El port corre su logica (n2a_frame, dirigido por frame) y deja el PPU como ESTADO OBSERVABLE;
// aqui se INFIEREN intenciones y se traducen al HW del Amiga:
//   - nametable + atributos -> banco planar de metatiles 16x16 (4 planos = pixel(2) + subpaleta(2))
//   - el 68000 solo MONTA el metatile cambiado; el BLITTER lo copia al playfield (TileBlockCopy)
//   - paleta BG ($3F00-$3F0F) -> COLOR00..15
// Los sprites (OAM) iran por sprites HW + Copper.
//
//   bash ./tools/build/build-demo.sh demos/nes/dk_port --debug
//   bash ./tools/run/run-demo.sh demos/nes/dk_port --warp

#include <eng/api/api.hpp>
#include <eng/graphics/tile_planar.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/input_poll.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <exec/execbase.h>
#include <proto/exec.h>

#include "support/gcc8_c_support.h"

// El port decompilado (genera PRG_ROM, CHR_ROM, f_*, n2a_frame, n2a_ppu_*).
#define N2A_NO_HOST_MAIN
#include "dk_port.hpp"

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

namespace gfx = eng::graphics::composition;

// Resolucion NES y 4 planos (16 colores = 4 subpaletas BG de 4).
constexpr eng::u16 kW = 256u;
constexpr eng::u16 kH = 240u;
constexpr eng::u8 kPlanes = 4u;
constexpr eng::u16 kRowBytes = kW / 8u;          // 32 bytes/fila de plano
constexpr eng::u16 kCellsX = 32u;                // tiles NES 8x8
constexpr eng::u16 kCellsY = 30u;
constexpr eng::u16 kTileStride = 16u;            // 2 planos x 8 filas por tile NES
constexpr eng::u16 kTileCount = 256u;

// Metatiles 16x16 (2x2 tiles NES): 16 x 15 en pantalla.
constexpr eng::u16 kMtX = 16u;
constexpr eng::u16 kMtY = 15u;
constexpr eng::u16 kMtCount = kMtX * kMtY;       // 240
// Bloque planar por metatile: 4 planos x (16 filas x 1 palabra) = 4 x 32 bytes.
constexpr eng::u16 kMtPlaneBytes = 32u;
constexpr eng::u16 kMtBytes = kPlanes * kMtPlaneBytes; // 128
constexpr eng::u16 kTileBankBytes = kTileCount * kTileStride; // 4096
constexpr eng::u16 kMtBankBytes = kMtCount * kMtBytes;        // 30720
constexpr bool kUseBlitter = true;                             // depuracion: CPU vs Blitter

// Sprites HW (OAM NES): sprite byte (16 px de ancho = hpos en bytes de 16px), 2 planos, y el
// canal lleva DAT+DATB por linea + 2 palabras a cero que terminan el DMA (AHRM 3a).
// Los sprites NES 8x8 se colocan en sprites HW de 8 px de ancho (1 palabra por linea).
constexpr eng::u16 kSpriteWordsPerLine = 2u;                  // DAT + DATB (16 px)
constexpr eng::u8  kSpriteHeight = 16u;                       // altura fija del canal
constexpr eng::u16 kSpriteInstanceWords = kSpriteHeight * kSpriteWordsPerLine + 2u; // 34
constexpr eng::u8  kSpriteChannels = 8u;
constexpr eng::u16 kSpriteBankWords = kSpriteChannels * kSpriteInstanceWords;       // 272
// El plano de scroll llega hasta y=240 (15 metatiles); el display empieza en la linea 0x2c.
constexpr eng::u16 kDisplayVStart = 0x2cu;                    // 44 (como 062/classic NES centrado)

// Paleta maestra NES (2C02), RGB 8-bit -> palabra Amiga 0RGB444. Indice = color NES (0..63).
constexpr eng::u8 kNesRgb[64][3] = {
	{84,84,84},{0,30,116},{8,16,144},{48,0,136},{68,0,100},{92,0,48},{84,4,0},{60,24,0},
	{32,42,0},{8,58,0},{0,64,0},{0,60,0},{0,50,60},{0,0,0},{0,0,0},{0,0,0},
	{152,150,152},{8,76,196},{48,50,236},{92,30,228},{136,20,176},{160,20,100},{152,34,32},{120,60,0},
	{84,90,0},{40,114,0},{8,124,0},{0,118,40},{0,102,120},{0,0,0},{0,0,0},{0,0,0},
	{236,238,236},{76,154,236},{120,124,236},{176,98,236},{228,84,236},{236,88,180},{236,106,100},{212,136,32},
	{160,170,0},{116,196,0},{76,208,32},{56,204,108},{56,180,204},{60,60,60},{0,0,0},{0,0,0},
	{236,238,236},{168,204,236},{188,188,236},{212,178,236},{236,174,236},{236,174,212},{236,180,176},{228,196,144},
	{204,210,120},{180,222,120},{168,226,144},{152,226,180},{160,214,228},{160,162,160},{0,0,0},{0,0,0},
};
constexpr eng::u16 nes_to_amiga(eng::u8 c) {
	return static_cast<eng::u16>(((kNesRgb[c][0] >> 4) << 8) | ((kNesRgb[c][1] >> 4) << 4) |
				     (kNesRgb[c][2] >> 4));
}

struct DkPortGame {
	eng::amiga::AmigaBackend* m_backend = nullptr;
	gfx::Scene m_scene {};
	eng::graphics::FramePlan m_plan {};
	eng::Block<eng::TileBankTag> m_tiles {};  // banco planar NES (2 planos/tile)
	eng::Block<eng::PlaneTag> m_mt {};        // banco de metatiles 4-planos (origen del Blitter)
	eng::Block<eng::CopperTag> m_copper {};   // lista de Copper (planos + paleta + sprites)
	eng::Block<eng::SpriteTag> m_sprdata {};  // DATA de los 8 canales de sprite HW
	eng::graphics::SpriteManager m_sprites {};
	eng::Palette32 m_pal {};
	bool m_ready = false;
	bool m_copper_ok = false;
	bool m_sprites_on = true; // etapa de sprites HW (OAM -> canales)
	eng::u16 m_frames = 0u;
	eng::u16 m_batch = 0u;       // jobs acumulados en el FramePlan actual (vaciado antes del tope)
	eng::u16 m_tilebase = 0u;    // base de la nametable en VRAM ($2000/$2400)
	eng::u32 m_chr_base = 0u;    // base de la pattern table del BG en el CHR ($1000 si ctrl bit4)
	eng::u32 m_spr_base = 0u;    // base de la pattern table de sprites ($1000 si ctrl bit3)
	eng::u8 m_mt_dirty[kMtCount] {};
	eng::u8 m_pal_spr[kSpriteChannels][4] {}; // subpaleta de sprite por canal (COLOR16+ par)

	eng::u8* tile_at(eng::u16 t) { return m_tiles.view.data() + static_cast<eng::u32>(t) * kTileStride; }
	eng::u8* mt_at(eng::u16 idx) { return m_mt.view.data() + static_cast<eng::u32>(idx) * kMtBytes; }

	// Decodifica el CHR (2bpp) de la NES al formato planar del engine (2 planos, 8 filas).
	void build_tile_bank() {
		for (eng::u16 t = 0; t < kTileCount; ++t) {
			// Los tiles de BG se leen de la pattern table seleccionada por $2000 bit 4
			// (`m_chr_base`); sin esto se pinta la tabla de sprites.
			(void)eng::graphics::decode_2bpp_planar(
				&CHR_ROM[m_chr_base + static_cast<eng::u32>(t) * 16u],
				tile_at(t), 8u, 8u, 2u, 1u, 8u);
		}
	}

	// Subpaleta BG (0..3) para la celda (cx,cy), segun la tabla de atributos NES.
	eng::u8 cell_pal(eng::u16 cx, eng::u16 cy) const {
		const eng::u16 a = static_cast<eng::u16>(m_tilebase + 0x3C0u + (cy / 4u) * 8u + (cx / 4u));
		const eng::u8 byte = n2a_ppu_vram(a);
		const eng::u8 shift = static_cast<eng::u8>(((cy % 4u) / 2u) * 4u + ((cx % 4u) / 2u) * 2u);
		return static_cast<eng::u8>((byte >> shift) & 3u);
	}

	// Monta el bloque planar 4-planos del metatile (mx,my): pixel(bits 0-1) + subpaleta(bits 2-3).
	void compose_metatile(eng::u16 mx, eng::u16 my) {
		eng::u8* const blk = mt_at(static_cast<eng::u16>(my * kMtX + mx));
		for (eng::u16 qy = 0; qy < 2u; ++qy) {
			const eng::u16 cy = static_cast<eng::u16>(my * 2u + qy);
			for (eng::u16 qx = 0; qx < 2u; ++qx) {
				const eng::u16 cx = static_cast<eng::u16>(mx * 2u + qx);
				const eng::u16 tile = n2a_ppu_vram(static_cast<eng::u16>(
					m_tilebase + cy * kCellsX + cx));
				const eng::u8 pal = cell_pal(cx, cy);
				const eng::u8* const src = tile_at(tile);
				const eng::u8 p2 = static_cast<eng::u8>((pal & 1u) ? 0xFFu : 0x00u);
				const eng::u8 p3 = static_cast<eng::u8>((pal & 2u) ? 0xFFu : 0x00u);
				const eng::u8 hb = static_cast<eng::u8>(qx == 0u ? 0u : 1u); // byte alto/bajo
				for (eng::u16 r = 0; r < 8u; ++r) {
					const eng::u32 o = static_cast<eng::u32>(qy * 8u + r) * 2u + hb;
					blk[0u * kMtPlaneBytes + o] = src[r];
					blk[1u * kMtPlaneBytes + o] = src[8u + r];
					blk[2u * kMtPlaneBytes + o] = p2;
					blk[3u * kMtPlaneBytes + o] = p3;
				}
			}
		}
	}

	// Trabajo de Blitter: copia el metatile planar a la pantalla (16x16, 4 planos).
	eng::graphics::BlitJob make_mt_job(eng::u16 mx, eng::u16 my) {
		const eng::u16 idx = static_cast<eng::u16>(my * kMtX + mx);
		const eng::u8* src = m_mt.view.data() + static_cast<eng::u32>(idx) * kMtBytes;
		eng::u8* dst = m_scene.bitplanes().data() +
			       static_cast<eng::u32>(my) * 16u * kRowBytes + static_cast<eng::u32>(mx) * 2u;
		return eng::graphics::BlitJob {
			eng::graphics::BlitJobKind::TileBlockCopy,
			eng::graphics::BlitPtr {},
			eng::graphics::BlitPtr::from_storage(reinterpret_cast<const eng::u16*>(src)),
			eng::graphics::BlitPtr::from_storage(
				reinterpret_cast<const eng::u16*>(dst)),
			1u,   // 16 px = 1 palabra por fila
			16u,  // 16 filas
			0,
			static_cast<eng::s16>(kRowBytes - 2u), // modulo destino entre filas
			kPlanes,
			0,
			kMtPlaneBytes,                          // stride de plano en origen (32)
			static_cast<eng::u32>(m_scene.plane_bytes()),
		};
	}

	// Render alternativo por CPU (depuracion): pega el metatile planar ya compuesto en pantalla.
	void paste_mt_cpu(eng::u16 mx, eng::u16 my) {
		const eng::u8* blk = mt_at(static_cast<eng::u16>(my * kMtX + mx));
		eng::u8* const bp = m_scene.bitplanes().data();
		const eng::u32 pb = m_scene.plane_bytes();
		for (eng::u16 p = 0; p < kPlanes; ++p) {
			for (eng::u16 r = 0; r < 16u; ++r) {
				const eng::u32 o = static_cast<eng::u32>(my * 16u + r) * kRowBytes +
						   static_cast<eng::u32>(mx) * 2u;
				bp[static_cast<eng::u32>(p) * pb + o] = blk[p * kMtPlaneBytes + r * 2u];
				bp[static_cast<eng::u32>(p) * pb + o + 1u] = blk[p * kMtPlaneBytes + r * 2u + 1u];
			}
		}
	}

	// Añade un job al plan. IMPORTANTE: `FramePlan::add_*` marca el plan NO-OK al llenarse
	// (max_blit_jobs), y `execute_frame_plan` RECHAZA un plan no-ok -> si vaciamos "cuando falla
	// el add", el lote ya esta marcado no-ok y se PIERDE. Hay que vaciar ANTES de llenar; por eso
	// llevamos `m_batch` y vaciamos a 64 (holgadamente por debajo del tope de 128).
	void add_mt_job(eng::u16 mx, eng::u16 my) {
		if (m_batch >= 64u) {
			(void)m_backend->execute_frame_plan(m_plan);
			m_plan.clear();
			m_batch = 0u;
		}
		(void)m_plan.add_tile_block_copy(make_mt_job(mx, my));
		++m_batch;
	}

	void blit_all() {
		m_plan.clear();
		m_batch = 0u;
		for (eng::u16 my = 0; my < kMtY; ++my) {
			for (eng::u16 mx = 0; mx < kMtX; ++mx) {
				if (kUseBlitter) {
					add_mt_job(mx, my);
				} else {
					paste_mt_cpu(mx, my);
				}
			}
		}
		if (kUseBlitter) {
			(void)m_backend->execute_frame_plan(m_plan);
		}
	}

	// Marca como sucio el metatile que contiene la celda (cx,cy).
	void dirty_cell(eng::u16 cx, eng::u16 cy) {
		if (cx >= kCellsX || cy >= kCellsY) {
			return;
		}
		if (cx < kMtX * 2u && cy < kMtY * 2u) {
			m_mt_dirty[(cy / 2u) * kMtX + (cx / 2u)] = 1u;
		}
	}

	void mark_all_dirty() {
		for (eng::u16 i = 0; i < kMtCount; ++i) {
			m_mt_dirty[i] = 1u;
		}
	}

	void rebuild_dirty() {
		m_plan.clear();
		m_batch = 0u;
		for (eng::u16 my = 0; my < kMtY; ++my) {
			for (eng::u16 mx = 0; mx < kMtX; ++mx) {
				const eng::u16 idx = static_cast<eng::u16>(my * kMtX + mx);
				if (m_mt_dirty[idx]) {
					compose_metatile(mx, my);
					if (kUseBlitter) {
						add_mt_job(mx, my);
					} else {
						paste_mt_cpu(mx, my);
					}
					m_mt_dirty[idx] = 0u;
				}
			}
		}
		if (kUseBlitter) {
			(void)m_backend->execute_frame_plan(m_plan);
		}
	}

	// --- Sprites HW + Copper ----------------------------------------------------------------
	// Escribe el DATA del canal `ch` a partir de un tile CHR (8x8, 2bpp): DAT/DATB por linea;
	// con flip horizontal se invierte el orden de bits. El sprite NES 8x8 ocupa 1 palabra/linea
	// (D=1, bits 0-7). El tile se coloca en las 8 lineas superiores del canal (altura 16).
	void build_sprite_channel(eng::u8 ch, eng::u16 tile, bool flip_h) {
		eng::u16* const data = m_sprdata.view.as_words().data();
		eng::u16* const inst = data + static_cast<eng::u32>(ch) * kSpriteInstanceWords;
		const eng::u8* const chr = &CHR_ROM[m_spr_base + static_cast<eng::u32>(tile) * 16u];
		for (eng::u16 line = 0; line < kSpriteHeight; ++line) {
			eng::u16 a = 0u;
			eng::u16 b = 0u;
			if (line < 8u) {
				const eng::u8 p0 = chr[line];
				const eng::u8 p1 = chr[8u + line];
				if (flip_h) {
					for (eng::u8 i = 0; i < 8u; ++i) {
						a = static_cast<eng::u16>(a | (((p0 >> i) & 1u) << (7u - i)));
						b = static_cast<eng::u16>(b | (((p1 >> i) & 1u) << (7u - i)));
					}
				} else {
					a = p0;
					b = p1;
				}
			}
			inst[line * 2u + 0u] = static_cast<eng::u16>(a << 8u);
			inst[line * 2u + 1u] = static_cast<eng::u16>(b << 8u);
		}
		const eng::u16 tail = static_cast<eng::u16>(kSpriteHeight) * kSpriteWordsPerLine;
		inst[tail + 0u] = 0u;
		inst[tail + 1u] = 0u;
	}

	// Refresca los 8 canales HW desde la OAM (primeros 8 sprites con y < $F0).
	void build_sprites_from_oam() {
		m_sprites.disable_all();
		const eng::u16* const data = m_sprdata.view.as_words().data();
		for (eng::u8 ch = 0; ch < kSpriteChannels; ++ch) {
			const eng::u8 oy = n2a_ppu_oam(static_cast<eng::u8>(ch * 4u + 0u));
			const eng::u8 tile = n2a_ppu_oam(static_cast<eng::u8>(ch * 4u + 1u));
			const eng::u8 attr = n2a_ppu_oam(static_cast<eng::u8>(ch * 4u + 2u));
			const eng::u8 ox = n2a_ppu_oam(static_cast<eng::u8>(ch * 4u + 3u));
			if (oy >= 0xF0u) {
				continue; // sprite fuera de pantalla en la OAM NES
			}
			build_sprite_channel(ch, tile, (attr & 0x40u) != 0u);
			// y NES 0..239 -> linea Amiga +kDisplayVStart (misma geometria que el BG).
			eng::graphics::SpriteConfig cfg {};
			cfg.enabled = true;
			cfg.data = eng::Span<const eng::u16> {
				data + static_cast<eng::u32>(ch) * kSpriteInstanceWords, kSpriteInstanceWords};
			cfg.width_words = 1u;
			cfg.height = kSpriteHeight;
			cfg.hpos = static_cast<eng::u16>(ox + 128u);
			cfg.vstart = static_cast<eng::u16>(oy + kDisplayVStart + 1u);
			cfg.vstop = static_cast<eng::u16>(cfg.vstart + kSpriteHeight - 1u);
			cfg.palette_base = 16u;
			m_sprites.set(ch, cfg);
			// Subpaleta de sprite ($3F10 + attr&3): COLOR16..19 (el par del canal no se puede
			// cambiar por canal, asi que se usa la subpaleta del sprite de mayor prioridad).
			const eng::u8 sp = static_cast<eng::u8>(attr & 3u);
			for (eng::u8 c = 0; c < 4u; ++c) {
				m_pal_spr[ch][c] = n2a_ppu_pal(static_cast<eng::u8>(0x10u + sp * 4u + c));
			}
		}
	}

	// Reconstruye la copperlist de la escena (planos + paleta + sprites) desde la OAM.
	void rebuild_scene_copper() {
		gfx::SceneResources res = gfx::planar(kW, kH, kPlanes);
		auto sprite_stage = [this](gfx::Scene& sc) {
			if (!m_sprites_on) {
				return;
			}
			// COLOR17..31 (0x1A2 + c*2): subpaletas de sprite; NO pisar COLOR02/03 del BG.
			for (eng::u8 c = 0; c < 8u; ++c) {
				sc.scheduler().move(static_cast<eng::copper::Register>(0x1A2u + c * 2u),
						    nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(0x11u + c))));
			}
			// Anade SPREN sin borrar los bits que ya activo `display()`
			// (DMACON usa SET/CLR: escribir DmaSet|DmaSprite borraria Bitplane/Copper).
			sc.scheduler().move(eng::copper::Register::DMACON,
					    static_cast<eng::u16>(0x8000u | eng::copper::DmaSprite));
			m_sprites.emit_into(sc.scheduler());
		};
		m_scene.begin_build();
		gfx::display(res)(m_scene);
		gfx::palette(eng::PaletteWords {m_pal.color, 16u}, 0u, 16u)(m_scene);
		sprite_stage(m_scene);
		m_copper_ok = m_scene.end_build();
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		m_backend = &backend;
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({192u * 1024u, 64u * 1024u, 16u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000D001u);
			return;
		}
		// 1) Correr el port unos frames para que escriba nametable/atributos/paleta.
		for (eng::u16 i = 0; i < 60u; ++i) {
			n2a_frame();
		}
		m_tilebase = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
		// Base de la pattern table del BG ($2000 bit 4) y de los sprites ($2000 bit 3).
		m_chr_base = (n2a_ppu_ctrl() & 0x10u) ? 0x1000u : 0u;
		m_spr_base = (n2a_ppu_ctrl() & 0x08u) ? 0x1000u : 0u;
		// 2) Paleta BG ($3F00-$3F0F) -> COLOR00..15.
		for (eng::u16 i = 0; i < 16u; ++i) {
			m_pal.color[i] = nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(i)));
		}
		for (eng::u16 i = 16u; i < 32u; ++i) {
			m_pal.color[i] = 0x000u;
		}
		// 3) Display planar 256x240, 4 planos. Etapas: display(res) (geometria) + paleta NES
		//    (16 colores) + una etapa propia que emite los sprites HW en la MISMA copperlist.
		gfx::SceneResources res = gfx::planar(kW, kH, kPlanes);
		auto sprite_stage = [this](gfx::Scene& sc) {
			// Subpaleta de sprites: COLOR17..31 (0x1A2 + c*2) = $3F11..$3F1F.
			for (eng::u8 c = 0; c < 8u; ++c) {
				sc.scheduler().move(static_cast<eng::copper::Register>(0x1A2u + c * 2u),
						    nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(0x11u + c))));
			}
			sc.scheduler().move(eng::copper::Register::DMACON,
					    static_cast<eng::u16>(0x8000u | eng::copper::DmaSprite));
			m_sprites.emit_into(sc.scheduler());
		};
		if (!gfx::compose(m_scene, backend.memory_manager(), res, gfx::ocs_a500,
				  gfx::display(res),
				  gfx::palette(eng::PaletteWords {m_pal.color, 16u}, 0u, 16u),
				  sprite_stage)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000D002u);
			return;
		}
		// 4) Bancos en CHIP (el Blitter solo lee de chip) + Copper + DATA de sprites.
		m_tiles = backend.memory_manager().chip().reserve<eng::TileBankTag>(kTileBankBytes, 16);
		m_mt = backend.memory_manager().chip().reserve<eng::PlaneTag>(kMtBankBytes, 16);
		m_copper = backend.memory_manager().chip().reserve<eng::CopperTag>(512u * 2u, 16);
		m_sprdata = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			static_cast<eng::u32>(kSpriteBankWords) * 2u, 16);
		if (!m_tiles.valid() || !m_mt.valid() || !m_copper.valid() || !m_sprdata.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000D003u);
			return;
		}
		build_tile_bank();
		// 5) Componer todos los metatiles y copiarlos con el Blitter.
		for (eng::u16 my = 0; my < kMtY; ++my) {
			for (eng::u16 mx = 0; mx < kMtX; ++mx) {
				compose_metatile(mx, my);
			}
		}
		// 6) Sprites HW: DATA inicial desde OAM y takeover de la escena (geometria+paleta+sprites).
		build_sprites_from_oam();
		rebuild_scene_copper();
		m_scene.takeover(backend);
		// Copiar los metatiles DESPUES del takeover (el buffer mostrado ya es el activo).
		blit_all();
		m_ready = true;
		// Diagnostico: publica el PPU observado tras 4 frames (host espera ctrl=90 mask=1E),
		// y el nº de sprites HW activos + estado del Copper.
		eng::debug::mark_ready(
			g_eng_run_status,
			0xD0000000u | (static_cast<eng::u32>(n2a_ppu_ctrl()) << 8u) |
				static_cast<eng::u32>(n2a_ppu_mask()) |
				(m_copper_ok ? 0x80000000u : 0u));
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}
		// Input: joystick Amiga -> botones NES (bit0=A,1=B,2=Select,3=Start,4=Up..7=Right).
		{
			eng::amiga::GameInput gin;
			eng::amiga::poll_input(gin);
			eng::u8 pad = 0u;
			if ((gin.port0 & eng::amiga::kJoyUp) != 0u) pad = static_cast<eng::u8>(pad | 0x10u);
			if ((gin.port0 & eng::amiga::kJoyDown) != 0u) pad = static_cast<eng::u8>(pad | 0x20u);
			if ((gin.port0 & eng::amiga::kJoyLeft) != 0u) pad = static_cast<eng::u8>(pad | 0x40u);
			if ((gin.port0 & eng::amiga::kJoyRight) != 0u) pad = static_cast<eng::u8>(pad | 0x80u);
			if ((gin.port0 & eng::amiga::kJoyFire) != 0u) pad = static_cast<eng::u8>(pad | 0x09u); // A + Start
			n2a_set_pad(pad);
		}
		// Logica del juego: un frame del port (dirigido por frame, cede al NMI).
		n2a_frame();
		++m_frames;
		// Intencion BG: celdas de nametable cambiadas -> metatiles sucios (nombre o atributos).
		eng::u16 addr = 0u;
		eng::u8 val = 0u;
		while (n2a_ppu_dirty_pop(&addr, &val)) {
			dirty_cell(static_cast<eng::u16>(addr % kCellsX),
				   static_cast<eng::u16>(addr / kCellsX));
		}
		// Copia con Blitter solo de los metatiles cambiados (y ejecuta el plan).
		rebuild_dirty();
		// Sprites HW desde la OAM: reemite la copperlist y la reinstala en VBlank.
		build_sprites_from_oam();
		rebuild_scene_copper();
		if (m_copper_ok) {
			backend.install_copper_list(m_scene.active_words());
		}
	}

	void render(eng::amiga::AmigaBackend&, eng::GameContext& context) {
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	DkPortGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
