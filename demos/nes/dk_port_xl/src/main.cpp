// Demo - PORT DK con render de BG por X-limited (scroll HW por Copper) + sprites HW.
//
// Idea: el nametable NES se trata como la fuente de un mapa de tiles; los metatiles (16x16)
// se hornean a un banco planar y el compositor X-limited del engine hace el scroll por
// Copper (BPLxPT/BPL1MOD), igual que la NES desplaza el PPU. Los sprites NES (OAM) van por
// canales HW con la X compensada por el scroll.
//
//   bash ./tools/build/build-demo.sh demos/nes/dk_port_xl --debug
//   bash ./tools/run/run-demo.sh demos/nes/dk_port_xl --warp

#include <eng/api/api.hpp>
#include <eng/platform/amiga/backend.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_source.hpp>
#include <eng/graphics/sprite_manager.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/platform/amiga/input_poll.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

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

namespace playfield = eng::playfield;

// Metatile 16x16 -> 16x16 px de pantalla. NES: 32x30 celdas 8x8 = 16x15 metatiles.
constexpr eng::u16 kTileW = 16u;
constexpr eng::u16 kTileH = 16u;
constexpr eng::u16 kViewportW = 256u;                // ancho util NES (256 px)
constexpr eng::u16 kViewportH = 240u;                // alto util NES (240 px)
constexpr eng::u8  kPlanes = 4u;
constexpr eng::u16 kDisplayH = 240u;                 // y_mode=Off -> sin split
constexpr eng::u16 kMtCols = 20u;                    // >= viewport/tile (16) + margen del anillo
constexpr eng::u16 kMtRows = 15u;                    // 15 metatiles de alto (240 px)
constexpr eng::u16 kScreenCols = 16u;                // la pantalla NES real: 16 metatiles (256 px)
constexpr eng::u16 kTilesetCount = 64u;              // generativo: 16 glyph x 4 variantes

constexpr playfield::ScrollConsts kScrollConsts {
	kTileW, kTileH, kDisplayH,
	static_cast<eng::u32>(kDisplayH) * kPlanes, kPlanes,
};

// Paleta NES 2C02 -> Amiga 0RGB444.
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

// Metatiles del mundo (celdas de 16x16): indice 0..63. 0xFFFF = vacio.
eng::u16 g_cells[kMtCols * kMtRows] {};

// El row_fn generativo solo recibe (glyph, variant) = indice de metatile 0..63, asi que
// horneamos una tabla descriptora por metatile: los 4 tiles NES (TL,TR,BL,BR) y su
// subpaleta NES (0..3) â€” el nametable NES no sigue ningun patron TL/TL+1.
// Tabla de metatiles UNICOS (sin tope de 64): cada uno guarda los 4 tiles NES (TL,TR,BL,BR)
// reales del nametable (no vecinos forzados) y su subpaleta. El banco se construye en Chip
// (prebuilt) dimensionado a `g_mt_count`, referenciado por el mapa.
constexpr eng::u16 kMaxMt = 512u;
eng::u8 g_mt4[kMaxMt][4] {};
eng::u8 g_mtpal[kMaxMt] {};
eng::u16 g_mt_count = 0u;

// Base de la pattern table del BG en el CHR (bits 4 de $2000): 0x0000 o 0x1000. DK usa
// $1000 para el BG -> sin esto se pinta el tile equivocado (la tabla de sprites).
eng::u32 g_bg_chr = 0u;

// Palabra planar (16 px) de la fila `y`/plano `plane` del metatile `m`.
eng::u16 mt_word(eng::u16 m, eng::u8 y, eng::u8 plane) {
	const eng::u8 pal = g_mtpal[m];
	const bool lower = (y >= 8u);
	const eng::u8 row8 = static_cast<eng::u8>(y & 7u);
	const eng::u8 tl = g_mt4[m][lower ? 2u : 0u];
	const eng::u8 tr = g_mt4[m][lower ? 3u : 1u];
	const eng::u32 bo = g_bg_chr + static_cast<eng::u32>(tl) * 16u;
	const eng::u32 br = g_bg_chr + static_cast<eng::u32>(tr) * 16u;
	const eng::u8 p0l = CHR_ROM[bo + row8];
	const eng::u8 p1l = CHR_ROM[bo + 8u + row8];
	const eng::u8 p0r = CHR_ROM[br + row8];
	const eng::u8 p1r = CHR_ROM[br + 8u + row8];
	switch (plane) {
	case 0u: return static_cast<eng::u16>((static_cast<eng::u16>(p0l) << 8u) | p0r);
	case 1u: return static_cast<eng::u16>((static_cast<eng::u16>(p1l) << 8u) | p1r);
	case 2u: return ((pal & 1u) ? 0xFFFFu : 0u);
	default: return ((pal & 2u) ? 0xFFFFu : 0u);
	}
}

/// Tamano del banco prebuilt X-limited (interleaved) para `count` metatiles de 16x16 y
/// `planes` planos: 40 B/planelinea, 16*planes planelineas por fila de 20 bloques.
eng::u32 bank_bytes(eng::u16 count, eng::u8 planes) {
	const eng::u32 blocks_per_row = 320u / 16u; // 20
	const eng::u32 rows = (count + blocks_per_row - 1u) / blocks_per_row;
	return rows * (16u * planes) * 40u;
}

/// Rellena el banco con el MISMO layout que `xlimited_build_blocks_bitmap`.
void fill_bank(eng::u8* d, eng::u16 count, eng::u8 planes) {
	const eng::u32 sbpr = 40u, bpr = 20u;
	for (eng::u16 m = 0; m < count; ++m) {
		const eng::u16 bx = static_cast<eng::u16>(m % bpr);
		const eng::u16 by = static_cast<eng::u16>(m / bpr);
		const eng::u32 base_pl = static_cast<eng::u32>(by) * (16u * planes) * sbpr;
		for (eng::u16 row = 0; row < 16u; ++row) {
			for (eng::u8 pl = 0; pl < planes; ++pl) {
				const eng::u16 word = mt_word(m, static_cast<eng::u8>(row), pl);
				const eng::u32 off = base_pl +
					static_cast<eng::u32>(row) * planes * sbpr +
					static_cast<eng::u32>(pl) * sbpr + bx * 2u;
				d[off] = static_cast<eng::u8>(word >> 8u);
				d[off + 1u] = static_cast<eng::u8>(word & 0xffu);
			}
		}
	}
}

eng::u16 g_palette[32] {}; // 0..15 = BG ($3F00-0F); 16..31 = sprites ($3F10-1F, COLOR16-31)

// Sprites HW desde la OAM: 8 canales, DATA = DAT+DATB por linea + 2 palabras de fin de DMA.
// Un sprite NES 8x8 ocupa 1 palabra/linea (16 px de ancho de canal, 8 usados).
constexpr eng::u8  kSpriteChannels = 8u;
constexpr eng::u16 kSpriteHeight = 16u;
constexpr eng::u16 kSpriteInstanceWords = kSpriteHeight * 2u + 2u; // 34
constexpr eng::u32 kSpriteDataBytes = kSpriteChannels * kSpriteInstanceWords * 2u;
// Cableado OAM->canales HW listo; posicion/paleta pendientes de calibrar (se ve un sprite
// desplazado). Desactivado por defecto para no ensuciar el render mientras se calibra.
constexpr bool kSpritesOn = false;
// Modo prueba de scroll: mueve la camara X para medir fps/coste real en A1200.
constexpr bool kScrollTest = true;   // sigue el scroll de la ROM ($2005/$2006) — NO un paso artificial
constexpr eng::s32 kScrollStep = 2;  // (obsoleto) paso artificial del modo test
constexpr bool kRunPort = true;     // false = solo render (test de coste del port vs render)
constexpr bool kDoCompose = true;  // false = saltar compose/install (test del bucle del engine)
eng::u32 g_spr_chr = 0u;              // base de la pattern table de sprites ($2000 bit 3)
constexpr eng::u16 kSpriteTop = 0x2Au; // DIWSTRT_y (0x29) + 1 (linea raster del primer pixel)

// Reconstruye el mapa de metatiles a partir del nametable NES (tilebase actual): los 4 tiles
// NES REALES de cada bloque 2x2 (no vecinos forzados) y su subpaleta (del cuadrante de atributos).
void rebuild_world_from_port() {
	const eng::u16 tb = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
	g_mt_count = 0u;
	for (eng::u16 ty = 0; ty < kMtRows; ++ty) {
		for (eng::u16 tx = 0; tx < kScreenCols; ++tx) {
			const eng::u16 cx = static_cast<eng::u16>(tx * 2u);
			const eng::u16 cy = static_cast<eng::u16>(ty * 2u);
			// Los 4 tiles NES del metatile (TL, TR, BL, BR) del nametable.
			eng::u8 t4[4];
			t4[0] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + cy * 32u + cx)));
			t4[1] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + cy * 32u + cx + 1u)));
			t4[2] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + (cy + 1u) * 32u + cx)));
			t4[3] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + (cy + 1u) * 32u + cx + 1u)));
			const eng::u16 attr_addr = static_cast<eng::u16>(tb + 0x3C0u + (cy / 4u) * 8u + (cx / 4u));
			const eng::u8 ab = n2a_ppu_vram(attr_addr);
			const eng::u8 pal = static_cast<eng::u8>((ab >> (((cy % 4u) / 2u) * 4u + ((cx % 4u) / 2u) * 2u)) & 3u);
			// Dedupe contra TODOS los metatiles ya vistos (sin tope de 64).
			eng::u16 idx = 0xFFFFu;
			for (eng::u16 m = 0; m < g_mt_count; ++m) {
				if (g_mtpal[m] != pal) continue;
				if (g_mt4[m][0] == t4[0] && g_mt4[m][1] == t4[1] &&
				    g_mt4[m][2] == t4[2] && g_mt4[m][3] == t4[3]) {
					idx = m;
					break;
				}
			}
			if (idx == 0xFFFFu && g_mt_count < kMaxMt) {
				idx = g_mt_count++;
				g_mt4[idx][0] = t4[0]; g_mt4[idx][1] = t4[1];
				g_mt4[idx][2] = t4[2]; g_mt4[idx][3] = t4[3];
				g_mtpal[idx] = pal;
			}
			g_cells[static_cast<eng::u32>(ty) * kMtCols + tx] = (idx == 0xFFFFu) ? 0xFFFFu : idx;
		}
		// Columnas del anillo mas alla de la pantalla NES: repite la pantalla (toroide),
		// para que las 22 columnas que lee el compositor X-limited esten definidas.
		for (eng::u16 tx = kScreenCols; tx < kMtCols; ++tx) {
			g_cells[static_cast<eng::u32>(ty) * kMtCols + tx] =
				g_cells[static_cast<eng::u32>(ty) * kMtCols + ((tx - kScreenCols) % kScreenCols)];
		}
	}
}

// Recalcula UN metatile (tx,ty) desde el nametable actual y lo registra (dedupe). Devuelve su
// indice (o 0xFFFF si la tabla esta llena). Lo usan el rebuild incremental de celdas sucias.
eng::u16 compute_metatile(eng::u16 tx, eng::u16 ty) {
	const eng::u16 tb = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
	const eng::u16 cx = static_cast<eng::u16>(tx * 2u);
	const eng::u16 cy = static_cast<eng::u16>(ty * 2u);
	eng::u8 t4[4];
	t4[0] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + cy * 32u + cx)));
	t4[1] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + cy * 32u + cx + 1u)));
	t4[2] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + (cy + 1u) * 32u + cx)));
	t4[3] = static_cast<eng::u8>(n2a_ppu_vram(static_cast<eng::u16>(tb + (cy + 1u) * 32u + cx + 1u)));
	const eng::u16 attr_addr = static_cast<eng::u16>(tb + 0x3C0u + (cy / 4u) * 8u + (cx / 4u));
	const eng::u8 ab = n2a_ppu_vram(attr_addr);
	const eng::u8 pal = static_cast<eng::u8>((ab >> (((cy % 4u) / 2u) * 4u + ((cx % 4u) / 2u) * 2u)) & 3u);
	for (eng::u16 m = 0; m < g_mt_count; ++m) {
		if (g_mtpal[m] != pal) continue;
		if (g_mt4[m][0] == t4[0] && g_mt4[m][1] == t4[1] &&
		    g_mt4[m][2] == t4[2] && g_mt4[m][3] == t4[3]) {
			return m;
		}
	}
	if (g_mt_count < kMaxMt) {
		const eng::u16 idx = g_mt_count++;
		g_mt4[idx][0] = t4[0]; g_mt4[idx][1] = t4[1];
		g_mt4[idx][2] = t4[2]; g_mt4[idx][3] = t4[3];
		g_mtpal[idx] = pal;
		return idx;
	}
	return 0xFFFFu;
}

// **Rebuild INCREMENTAL**: consume las celdas sucias del PPU y recomputa SOLO los metatiles
// afectados (en vez de los 16x15 completos con dedupe O(n^2)). Devuelve `true` si algo cambio.
// El llamador debe rellenar el banco y re-blitear (aqui se mantiene simple: fill completo).
bool update_world_from_dirty() {
	const eng::u16 tb = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
	bool any = false;
	eng::u16 da = 0u;
	eng::u8 dv = 0u;
	while (n2a_ppu_dirty_pop(&da, &dv)) {
		if (da < tb || da >= static_cast<eng::u16>(tb + 0x3C0u)) { continue; } // no es el nametable BG
		const eng::u16 cell = static_cast<eng::u16>(da - tb);
		const eng::u16 col = static_cast<eng::u16>(cell % 32u);
		const eng::u16 row = static_cast<eng::u16>(cell / 32u);
		if (col >= kScreenCols * 2u || row >= kMtRows * 2u) { continue; }
		const eng::u16 tx = static_cast<eng::u16>(col / 2u);
		const eng::u16 ty = static_cast<eng::u16>(row / 2u);
		const eng::u16 idx = compute_metatile(tx, ty);
		if (idx == 0xFFFFu) { continue; }
		for (eng::u16 c = tx; c < kMtCols; c = static_cast<eng::u16>(c + kScreenCols)) {
			g_cells[static_cast<eng::u32>(ty) * kMtCols + c] = idx;
		}
		any = true;
	}
	return any;
}
struct DkXlGame {
	playfield::XlimitedScene<kScrollConsts, playfield::TileLayerMap, playfield::ScrollProgressive> scene {};
	playfield::XlimitedSceneConfigT<playfield::TileLayerMap> cfg {};
	eng::Block<eng::TileBankTag> m_bank {};  // banco prebuilt de metatiles (Chip)
	eng::graphics::FramePlan plan {};
	eng::s32 cam_x = 0;
	eng::s32 cam_y = 0;
	bool ready = false;
	eng::u16 m_frames = 0u;
	eng::u16 m_rebuilds = 0u; // nÂº de rebuilds del mapa (cambios de nametable)

	// Refresca los 8 canales HW desde la OAM (los primeros 8 sprites con y<$F0). Cada sprite NES
	// se asigna a un canal del PAR de su subpaleta (attr&3), porque cada par comparte la gama
	// COLOR16+s*4. DATA: tile 8x8 en las 8 lineas altas del canal (D=1, MSB=izquierda).
	void build_sprites() {
		eng::graphics::SpriteManager& sm = scene.sprites();
		sm.disable_all();
		eng::u16* const data = reinterpret_cast<eng::u16*>(sm.sprite_data().data());
		eng::u8 used[kSpriteChannels] = {};
		for (eng::u8 i = 0; i < 64u; ++i) {
			const eng::u8 oy = n2a_ppu_oam(static_cast<eng::u8>(i * 4u + 0u));
			const eng::u8 tile = n2a_ppu_oam(static_cast<eng::u8>(i * 4u + 1u));
			const eng::u8 attr = n2a_ppu_oam(static_cast<eng::u8>(i * 4u + 2u));
			const eng::u8 ox = n2a_ppu_oam(static_cast<eng::u8>(i * 4u + 3u));
			if (oy >= 0xF0u) {
				continue;
			}
			const eng::u8 s = static_cast<eng::u8>(attr & 3u);
			eng::s8 ch = -1;
			for (eng::u8 c = static_cast<eng::u8>(s * 2u);
			     c < static_cast<eng::u8>(s * 2u + 2u) && c < kSpriteChannels; ++c) {
				if (used[c] == 0u) { ch = static_cast<eng::s8>(c); break; }
			}
			if (ch < 0) {
				continue; // no hay canal libre para esa subpaleta
			}
			used[ch] = 1u;
			eng::u16* const inst = data + static_cast<eng::u32>(ch) * kSpriteInstanceWords;
			const eng::u8* const chr = &CHR_ROM[g_spr_chr + static_cast<eng::u32>(tile) * 16u];
			const bool flip = (attr & 0x40u) != 0u;
			for (eng::u16 line = 0; line < kSpriteHeight; ++line) {
				eng::u16 a = 0u, b = 0u;
				if (line < 8u) {
					a = chr[line];
					b = chr[8u + line];
					if (flip) {
						eng::u16 fa = 0u, fb = 0u;
						for (eng::u8 k = 0; k < 8u; ++k) {
							fa = static_cast<eng::u16>(fa | (((a >> k) & 1u) << (7u - k)));
							fb = static_cast<eng::u16>(fb | (((b >> k) & 1u) << (7u - k)));
						}
						a = fa;
						b = fb;
					}
				}
				inst[line * 2u + 0u] = static_cast<eng::u16>(a << 8u);
				inst[line * 2u + 1u] = static_cast<eng::u16>(b << 8u);
			}
			inst[16u * 2u + 0u] = 0u; // fin de DMA (2 palabras a cero)
			inst[16u * 2u + 1u] = 0u;
			eng::graphics::SpriteConfig cfg {};
			cfg.enabled = true;
			cfg.data = eng::Span<const eng::u16> {inst, kSpriteInstanceWords};
			cfg.width_words = 1u;
			cfg.height = kSpriteHeight;
			cfg.hpos = static_cast<eng::u16>(ox + 128u);
			cfg.vstart = static_cast<eng::u16>(oy + kSpriteTop);
			cfg.vstop = static_cast<eng::u16>(cfg.vstart + kSpriteHeight - 1u);
			cfg.palette_base = 16u;
			sm.set(static_cast<eng::u8>(ch), cfg);
		}
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({256u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E001u);
			return;
		}
		// Arranca el port unos frames para que escriba el nametable y la paleta.
		for (eng::u16 i = 0; i < 60u; ++i) {
			n2a_frame();
		}
		// Base de las pattern tables ($2000): bit 4 = BG, bit 3 = sprites. DK: BG $1000, sprites $0000.
		g_bg_chr = (n2a_ppu_ctrl() & 0x10u) ? 0x1000u : 0u;
		g_spr_chr = (n2a_ppu_ctrl() & 0x08u) ? 0x1000u : 0u;
		for (eng::u16 i = 0; i < 16u; ++i) {
			g_palette[i] = nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(i)));       // BG $3F00-0F
			g_palette[16u + i] = nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(0x10u + i))); // spr $3F10-1F
		}
		rebuild_world_from_port();
		// Banco prebuilt en Chip, dimensionado al MAXIMO de metatiles (para poder rebuilds de
		// pantalla sin reasignar): kMaxMt entradas.
		const eng::u32 bbytes = bank_bytes(kMaxMt, kPlanes);
		m_bank = backend.memory_manager().chip().reserve<eng::TileBankTag>(bbytes, 16);
		if (!m_bank.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E005u);
			return;
		}
		fill_bank(m_bank.view.data(), g_mt_count, kPlanes);

		cfg.viewport_w = kViewportW;
		cfg.viewport_h = kViewportH;
		cfg.tile_width = kTileW;
		cfg.tile_height = kTileH;
		cfg.planes = kPlanes;
		cfg.fetch_mode = 0;
		cfg.x_mode = playfield::AxisPolicy::Ring;
		cfg.y_mode = playfield::AxisPolicy::Off;
		cfg.direction = playfield::DirectionPolicy::Bidirectional;
		cfg.display_height = kDisplayH;
		cfg.max_step = 4;
		cfg.map.cells = eng::Span<const eng::u16> {g_cells, kMtCols * kMtRows};
		cfg.map.width = kMtCols;
		cfg.map.height = kMtRows;
		cfg.map.wrap_x = kMtCols;   // X toroidal
		cfg.map.wrap_y = 0;
		cfg.map.edge_tile = 0u;
		cfg.tileset_count = g_mt_count;
		cfg.fg_row_fn = nullptr;
		cfg.bg_row_fn = nullptr;
		// Banco prebuilt (aliaseado por la escena): sin tope de 64 y con los tiles reales.
		cfg.blocks_prebuilt = m_bank.view.data();
		cfg.blocks_prebuilt_size = bbytes;
		cfg.palette = eng::PaletteWords {g_palette, 32u}; // 0-15 BG, 16-31 sprites (COLOR16-31)
		cfg.sprite_data_bytes = kSpritesOn ? kSpriteDataBytes : 0u; // sprites HW (0 = no emitir)
		cfg.copper_bytes = 1536u;

		if (!scene.begin(backend.memory_manager(), cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E002u);
			return;
		}
		scene.bg().set_camera(0, 0);
		if (kSpritesOn) { build_sprites(); } // canales HW desde la OAM (DATA reservada por begin)
		plan.clear();
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E003u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E004u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0xE0000000u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
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
		// Logica del port (dirigida por frame).
		if (kRunPort) { n2a_frame(); }
		++m_frames;
		// REBUILD dinamico: si el juego cambio el nametable (transicion de pantalla), recomponer
		// el mapa de metatiles + el banco y re-blitear el anillo. Deteccion: celdas sucias del PPU.
		// REBUILD dinamico INCREMENTAL: consumir las celdas sucias del PPU y recomputar solo los
		// metatiles afectados (antes: rebuild completo 16x15 + dedupe O(n^2) cada frame).
		plan.clear();
		if (update_world_from_dirty()) {
			// Compactar si la tabla incremental ha crecido demasiado (evita llenar kMaxMt).
			if (g_mt_count > static_cast<eng::u16>(kMaxMt - 32u)) { rebuild_world_from_port(); }
			// Reconstruccion de pantalla: re-blitear el anillo (sin scroll ese frame).
			fill_bank(m_bank.view.data(), g_mt_count, kPlanes);
			if (!scene.fill(backend, plan)) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, 0x0000E012u);
				return;
			}
			m_rebuilds = static_cast<eng::u16>(m_rebuilds + 1u);
		} else if (kScrollTest) {
			// Scroll dirigido por la INTENCION de la ROM: seguir los registros del PPU que el
			// juego escribe ($2005/$2006 via el port). En el titulo de DK el scroll es 0 -> no se
			// mueve nada. Solo se engancha el scroll hardware cuando la ROM realmente lo cambia.
			const eng::s32 sx = static_cast<eng::s32>(n2a_ppu_scroll_x());
			const eng::s32 dx = sx - cam_x;
			if (dx != 0) {
				(void)scene.bg().update_scroll(plan, dx, 0);
				cam_x = sx;
			}
		}
		if (kSpritesOn) { build_sprites(); } // actualiza los canales HW desde la OAM cada frame
		plan.set_blit_budget_limits({8192, 16384, 4, 160});
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0000E010u);
			return;
		}
		if (kDoCompose && !scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0000E011u);
			return;
		}
		// Diagnostico fps: m_frames en bits 8-23, m_rebuilds en 24-31.
		g_eng_run_status.detail = 0xE0000000u | ((static_cast<eng::u32>(m_frames) & 0xffffu) << 8u) |
					  ((static_cast<eng::u32>(m_rebuilds) & 0xffu) << 24u);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (ready && kDoCompose) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	DkXlGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
