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
#include <eng/field/actor_target.hpp>
#include <eng/graphics/bob.hpp>
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
// Diagnostico temporal de la integracion Amiga (host vs Amiga): se leen por el
// canal lateral/GDB. No afecta al render.
__attribute__((used)) volatile eng::u32 g_dk_updates = 0u;
__attribute__((used)) volatile eng::u32 g_dk_pad = 0u;
__attribute__((used)) volatile eng::u32 g_dk_nmi = 0u;
__attribute__((used)) volatile eng::u32 g_dk_spr_placements = 0u;
__attribute__((used)) volatile eng::u32 g_dk_spr_overflow = 0u;
__attribute__((used)) volatile eng::u32 g_dk_spr_bobs = 0u;
struct FrameTelemetry {
	eng::u32 frame;
	eng::u16 blit_jobs;
	eng::u16 blit_words;
	eng::u16 copper_words;
	eng::u16 fillup_extra;
};
__attribute__((used, section(".data"))) volatile FrameTelemetry g_eng_frame_telemetry {};
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
constexpr eng::u16 kMtCols = 20u;                    // 16 pantalla + 4 de margen (DIW = 320px)
constexpr eng::u16 kMtRows = 15u;                    // 15 metatiles de alto (240 px)
constexpr eng::u16 kScreenCols = 16u;                // la pantalla NES real: 16 metatiles (256 px)
constexpr eng::u16 kMarginTx = 2u;                   // columnas de margen IZQUIERDA (centra 256 en 320)
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
constexpr eng::u16 kMaxMt = 1024u;
eng::u8 g_mt4[kMaxMt][4] {};
eng::u8 g_mtpal[kMaxMt] {};
eng::u16 g_mt_count = 0u;
// Hash de metatiles por (4 tiles, paleta): evita el dedupe O(n^2) en cada rebuild.
constexpr eng::u16 kMtHashSize = 2048u;
eng::u32 g_mt_hash_key[kMtHashSize] {};
eng::u16 g_mt_hash_idx[kMtHashSize] {}; // indice+1; 0 = vacio
eng::u16 g_mt_hash_used = 0u;

// Celdas (metatiles) cambiadas en el ultimo `update_world_from_dirty`, para el re-blit
// SELECTIVO (1 `add_draw` por celda cambiada, en vez de `fill` del anillo completo).
constexpr eng::u16 kChangedMax = 256u;
eng::u16 g_chg_tx[kChangedMax] {};
eng::u16 g_chg_ty[kChangedMax] {};
eng::u16 g_chg_n = 0u;

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

/// Rellena UNA entrada de metatile `m` del banco (mismo layout que `fill_bank`). Para el
/// rebuild incremental: solo se rellenan los metatiles NUEVOS, no los 512 del banco.
void fill_bank_one(eng::u8* d, eng::u16 m, eng::u8 planes) {
	const eng::u32 sbpr = 40u, bpr = 20u;
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

/// Rellena el banco con el MISMO layout que `xlimited_build_blocks_bitmap`.
void fill_bank(eng::u8* d, eng::u16 count, eng::u8 planes) {
	for (eng::u16 m = 0; m < count; ++m) { fill_bank_one(d, m, planes); }
}

eng::u16 g_palette[32] {}; // 0..15 = BG ($3F00-0F); 16..31 = sprites ($3F10-1F, COLOR16-31)

// Sprites HW desde la OAM: 8 canales, DATA = DAT+DATB por linea + 2 palabras de fin de DMA.
// Un sprite NES 8x8 u 8x16 ocupa 1 palabra/linea (16 px de ancho de canal, 8 usados).
constexpr eng::u8  kSpriteChannels = 8u;
constexpr eng::u16 kSpriteMaxHeight = 16u;
constexpr eng::u16 kSpriteMaxInstanceWords = kSpriteMaxHeight * 2u + 2u; // 34
constexpr eng::u16 kSprite8x8InstanceWords = 8u * 2u + 2u;               // 18
constexpr eng::u16 kMaxPlacements = 64u;
// Cache de instancias DMA por (tile, flips, 8x8/8x16): CHR es estatico, asi que cada
// combinacion se empaqueta UNA vez y los placements comparten el puntero. 128 ranuras
// cubren de sobra los <=64 sprites visibles de un frame.
constexpr eng::u16 kSpriteCacheSlots = 128u;
constexpr eng::u32 kSpriteCacheBytes = kSpriteCacheSlots * kSpriteMaxInstanceWords * 2u;
constexpr eng::u16 kSpritePreSlots = 256u * 4u;
constexpr eng::u32 kSpritePreBytes = kSpritePreSlots * kSprite8x8InstanceWords * 2u;
constexpr eng::u32 kSpritePre16Bytes = kSpritePreSlots * kSpriteMaxInstanceWords * 2u;
// BOB de overflow: hasta 8x16, 4 planos, ventana de 32 px. Hoja interleaved pair con
// 16 filas * 4 planos * 4 palabras = 256 palabras = 512 B (para 8x8 se usa la mitad).
constexpr eng::u8  kMaxBobs = 16u;
constexpr eng::u32 kBobBytes = 16u * 4u * 4u * 2u; // 512 B
// Cableado OAM->canales HW. Posicion calibrada (origen x=32, hpos=0x81+32+ox) y vstop
// exclusivo; queda validar visualmente la build A500 con -Os antes de activarlo.
constexpr bool kSpritesOn = true;
constexpr bool kAudioTestTone = false; // diagnostico: tono 440 Hz en canal 0
constexpr bool kAudioOn = true;       // puente APU->Paula
// Modo prueba de scroll: mueve la camara X para medir fps/coste real en A1200.
constexpr bool kScrollTest = true;   // sigue el scroll de la ROM ($2005/$2006) — NO un paso artificial
constexpr eng::s32 kScrollStep = 2;  // (obsoleto) paso artificial del modo test
constexpr bool kRunPort = true;     // false = solo render (test de coste del port vs render)
constexpr bool kDoCompose = true;  // false = saltar compose/install (test del bucle del engine)
// Prueba de gameplay automatizada: pulsa Start (0x08) desde el frame 30 durante 6 frames para
// entrar en la partida sin joystick (para capturas de referencia vs el oraculo).
constexpr bool kAutoStart = true;
eng::u32 g_spr_chr = 0u;              // base de la pattern table de sprites ($2000 bit 3)
constexpr eng::u16 kSpriteTop = 0x2Au; // DIWSTRT_y (0x29) + 1 (linea raster del primer pixel)

// Reconstruye el mapa de metatiles a partir del nametable NES (tilebase actual): los 4 tiles
// NES REALES de cada bloque 2x2 (no vecinos forzados) y su subpaleta (del cuadrante de atributos).
eng::u16 intern_metatile(const eng::u8 t4[4], eng::u8 pal);

void rebuild_world_from_port() {
	const eng::u16 tb = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
	g_mt_count = 0u;
	g_mt_hash_used = 0u;
	for (eng::u16 i = 0u; i < kMtHashSize; ++i) g_mt_hash_idx[i] = 0u;
	for (eng::u16 ty = 0; ty < kMtRows; ++ty) {
		// Fila entera VACIA; el juego se escribe desplazado kMarginTx (centrado en el DIW de 320px).
		for (eng::u16 tx = 0; tx < kMtCols; ++tx) {
			g_cells[static_cast<eng::u32>(ty) * kMtCols + tx] = 0xFFFFu;
		}
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
			const eng::u16 idx = intern_metatile(t4, pal);
			g_cells[static_cast<eng::u32>(ty) * kMtCols + (kMarginTx + tx)] = idx;
		}
	}
}

// Recalcula UN metatile (tx,ty) desde el nametable actual y lo registra (dedupe). Devuelve su
// indice (o 0xFFFF si la tabla esta llena). Lo usan el rebuild incremental de celdas sucias.
/// Interna un metatile en la tabla y devuelve su indice (o 0xFFFF si no cabe).
/// Hash directo con sondeo: el dedupe lineal escaneaba hasta 512 entradas por celda.
eng::u16 intern_metatile(const eng::u8 t4[4], eng::u8 pal) {
	const eng::u32 key = static_cast<eng::u32>(t4[0]) |
			     (static_cast<eng::u32>(t4[1]) << 8u) |
			     (static_cast<eng::u32>(t4[2]) << 16u) |
			     (static_cast<eng::u32>(t4[3]) << 24u);
	constexpr eng::u16 kMask = static_cast<eng::u16>(kMtHashSize - 1u);
	eng::u16 h = static_cast<eng::u16>((key ^ (key >> 13u) ^ (key >> 23u)) & kMask);
	for (eng::u16 probe = 0u; probe < kMtHashSize; ++probe) {
		const eng::u16 e = g_mt_hash_idx[h];
		if (e != 0u) {
			const eng::u16 idx = static_cast<eng::u16>(e - 1u);
			if (g_mt_hash_key[h] == key && g_mtpal[idx] == pal) {
				return idx;
			}
			h = static_cast<eng::u16>((h + 1u) & kMask);
			continue;
		}
		if (g_mt_count >= kMaxMt) return 0xFFFFu;
		const eng::u16 idx = g_mt_count++;
		g_mt4[idx][0] = t4[0]; g_mt4[idx][1] = t4[1];
		g_mt4[idx][2] = t4[2]; g_mt4[idx][3] = t4[3];
		g_mtpal[idx] = pal;
		g_mt_hash_key[h] = key;
		g_mt_hash_idx[h] = static_cast<eng::u16>(idx + 1u);
		++g_mt_hash_used;
		return idx;
	}
	return 0xFFFFu;
}

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
	return intern_metatile(t4, pal);
}

// **Rebuild INCREMENTAL**: consume las celdas sucias del PPU y recomputa SOLO los metatiles
// afectados (en vez de los 16x15 completos con dedupe O(n^2)). Devuelve `true` si algo cambio.
// El llamador debe rellenar el banco y re-blitear (aqui se mantiene simple: fill completo).
bool update_world_from_dirty() {
	const eng::u16 tb = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
	bool any = false;
	g_chg_n = 0u;
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
		// Columna del anillo = kMarginTx + tx (el juego va centrado en el DIW de 320px).
		const eng::u16 cx = static_cast<eng::u16>(kMarginTx + tx);
		g_cells[static_cast<eng::u32>(ty) * kMtCols + cx] = idx;
		{
			bool dup = false;
			for (eng::u16 k = 0; k < g_chg_n; ++k) {
				if (g_chg_tx[k] == cx && g_chg_ty[k] == ty) { dup = true; break; }
			}
			if (!dup && g_chg_n < kChangedMax) {
				g_chg_tx[g_chg_n] = cx; g_chg_ty[g_chg_n] = ty; ++g_chg_n;
			}
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
	eng::u32 m_pal_hash = 0u; // hash de la paleta NES ($3F00-1F) para detectar cambios
	eng::Block<eng::AudioTag, eng::MemoryKind::Chip> m_audio_buf {}; // 3 formas de onda para Paula
	eng::graphics::HwSpritePlacement m_placements[kMaxPlacements] {};
	eng::u8 m_placement_oam[kMaxPlacements] {}; // indice OAM (prioridad sprite-sprite)
	eng::u16 m_placement_count = 0u;
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_cache {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_pre {};
	eng::Block<eng::SpriteTag, eng::MemoryKind::Chip> m_sprite_pre16 {};
	eng::u32 m_pre_chr = 0u;
	bool m_pre_ok = false;
	bool m_pre16_ok = false;
	eng::u32 m_cache_key[kSpriteCacheSlots] {};
	eng::u32 m_cache_stamp[kSpriteCacheSlots] {};
	eng::u32 m_frame_stamp = 0u;
	eng::u16 m_pre_hits_frame = 0u;
	eng::Block<eng::BobTag, eng::MemoryKind::Chip> m_bob_data {};
	eng::u8 m_bob_x[kMaxBobs] {};
	eng::u8 m_bob_y[kMaxBobs] {};
	eng::u8 m_bob_h[kMaxBobs] {};
	eng::u8 m_bob_count = 0u;
	eng::u16 m_sprite_height = 8u;
	eng::u16 m_sprite_instance_words = kSprite8x8InstanceWords;
	bool m_sprite_tall = false;
	eng::u8 m_spr_to_bg[16] {}; // sprite palette index -> nearest BG palette index

	/// Mapa de color de emergencia si no se pudo reservar el quinto plano: cada color de las
	/// 4 subpaletas de sprite al indice de BG mas cercano. Con quinto plano se usa el indice
	/// exacto `sub*4+pixel` (COLOR16-31).
	void update_sprite_palette_map() {
		for (eng::u8 i = 0u; i < 16u; ++i) {
			const eng::u16 sc = g_palette[16u + i];
			const eng::u8 sr = static_cast<eng::u8>((sc >> 8u) & 0xFu);
			const eng::u8 sg = static_cast<eng::u8>((sc >> 4u) & 0xFu);
			const eng::u8 sb = static_cast<eng::u8>(sc & 0xFu);
			eng::u8 best = 0u;
			eng::u16 best_d = 0xFFFFu;
			for (eng::u8 j = 0u; j < 16u; ++j) {
				const eng::u16 bc = g_palette[j];
				const eng::s16 dr = static_cast<eng::s16>(sr) - static_cast<eng::s16>((bc >> 8u) & 0xFu);
				const eng::s16 dg = static_cast<eng::s16>(sg) - static_cast<eng::s16>((bc >> 4u) & 0xFu);
				const eng::s16 db = static_cast<eng::s16>(sb) - static_cast<eng::s16>(bc & 0xFu);
				const eng::u16 d = static_cast<eng::u16>(dr * dr + dg * dg + db * db);
				if (d < best_d) { best_d = d; best = j; }
			}
			m_spr_to_bg[i] = best;
		}
	}

	struct OamSprite {
		eng::u8 oy;
		eng::u8 tile;
		eng::u8 tile2;   // mitad inferior si `merged` (par vertical de 8x8)
		eng::u8 attr;
		eng::u8 ox;
		eng::u8 index;
		eng::u8 h;       // 8 u 16
		bool merged;
	};

	/// Tile CHR y fila de origen para una linea de la entrada OAM. Soporta 8x8 y 8x16
	/// (bit 5 de `$2000`): en 8x16 el bit 0 del tile elige la pattern table y el par
	/// de tiles es consecutivo; el flip vertical intercambia arriba/abajo.
	struct SpriteRow {
		const eng::u8* chr;
		eng::u8 row;
	};
	SpriteRow sprite_row(const OamSprite& o, eng::u16 line) const {
		const eng::u16 h = o.merged ? 16u : m_sprite_height;
		const eng::u16 l = (o.attr & 0x80u) ? static_cast<eng::u16>(h - 1u - line) : line;
		if (o.merged) {
			const eng::u8 tile = (l >= 8u) ? o.tile2 : o.tile;
			return { &CHR_ROM[g_spr_chr + static_cast<eng::u32>(tile) * 16u],
				 static_cast<eng::u8>(l & 7u) };
		}
		if (!m_sprite_tall) {
			return { &CHR_ROM[g_spr_chr + static_cast<eng::u32>(o.tile) * 16u],
				 static_cast<eng::u8>(l & 7u) };
		}
		const eng::u32 base = (o.tile & 1u) ? 0x1000u : 0u;
		const eng::u16 tile = static_cast<eng::u16>((o.tile & 0xFEu) + (l >= 8u ? 1u : 0u));
		return { &CHR_ROM[base + static_cast<eng::u32>(tile) * 16u],
			 static_cast<eng::u8>(l & 7u) };
	}

	/// Instancia DMA cacheada por (tile, flips, alto). CHR no cambia durante el juego;
	/// solo se empaqueta la primera vez que aparece cada combinacion. Las ranuras usadas
	/// en el frame actual no se evictan (stamp), asi que los punteros de los placements
	/// siguen vivos hasta `execute_frame_plan`.
	void write_instance(eng::u16* const inst, const OamSprite& o) {
		const eng::u16 h = o.merged ? 16u : m_sprite_height;
		const bool flip_h = (o.attr & 0x40u) != 0u;
		for (eng::u16 line = 0u; line < h; ++line) {
			const SpriteRow sr = sprite_row(o, line);
			eng::u16 a = sr.chr[sr.row];
			eng::u16 b = sr.chr[8u + sr.row];
			if (flip_h) {
				eng::u16 fa = 0u, fb = 0u;
				for (eng::u8 k = 0u; k < 8u; ++k) {
					fa = static_cast<eng::u16>(fa | (((a >> k) & 1u) << (7u - k)));
					fb = static_cast<eng::u16>(fb | (((b >> k) & 1u) << (7u - k)));
				}
				a = fa;
				b = fb;
			}
			inst[line * 2u + 0u] = static_cast<eng::u16>(a << 8u);
			inst[line * 2u + 1u] = static_cast<eng::u16>(b << 8u);
		}
		inst[h * 2u + 0u] = 0u;
		inst[h * 2u + 1u] = 0u;
	}

	const eng::u16* sprite_instance(const OamSprite& o) {
		const eng::u16 slot = static_cast<eng::u16>(o.tile * 4u +
			((o.attr & 0x40u) ? 1u : 0u) + ((o.attr & 0x80u) ? 2u : 0u));
		if (!o.merged && m_sprite_tall && m_pre16_ok) {
			++m_pre_hits_frame;
			return reinterpret_cast<const eng::u16*>(m_sprite_pre16.view.data() +
				static_cast<eng::u32>(slot) * kSpriteMaxInstanceWords * 2u);
		}
		if (!o.merged && !m_sprite_tall && m_pre_ok && g_spr_chr == m_pre_chr) {
			++m_pre_hits_frame;
			return reinterpret_cast<const eng::u16*>(m_sprite_pre.view.data() +
				static_cast<eng::u32>(slot) * kSprite8x8InstanceWords * 2u);
		}
		const eng::u32 key = static_cast<eng::u32>(
			o.tile | (static_cast<eng::u32>(o.tile2) << 8u) |
			((o.attr & 0x40u) ? 0x10000u : 0u) |
			((o.attr & 0x80u) ? 0x20000u : 0u) |
			(o.merged ? 0x40000u : 0u) |
			(m_sprite_tall ? 0x80000u : 0u) |
			(!m_sprite_tall && g_spr_chr != 0u ? 0x100000u : 0u));
		// Hash directo + sondeo lineal corto; el caso normal es 1-2 comparaciones.
		constexpr eng::u16 kMask = static_cast<eng::u16>(kSpriteCacheSlots - 1u);
		eng::u16 cslot = static_cast<eng::u16>(key & kMask);
		eng::u16 probe = 0u;
		for (; probe < kSpriteCacheSlots; ++probe) {
			if (m_cache_key[cslot] == key) {
				m_cache_stamp[cslot] = m_frame_stamp;
				return reinterpret_cast<const eng::u16*>(
					m_sprite_cache.view.data() +
					static_cast<eng::u32>(cslot) * kSpriteMaxInstanceWords * 2u);
			}
			if (m_cache_stamp[cslot] != m_frame_stamp) break; // ranura evictable
			cslot = static_cast<eng::u16>((cslot + 1u) & kMask);
		}
		if (probe == kSpriteCacheSlots) return nullptr; // no ocurre con <=64/frame
		eng::u16* const inst = reinterpret_cast<eng::u16*>(
			m_sprite_cache.view.data() +
			static_cast<eng::u32>(cslot) * kSpriteMaxInstanceWords * 2u);
		write_instance(inst, o);
		m_cache_key[cslot] = key;
		m_cache_stamp[cslot] = m_frame_stamp;
		return inst;
	}

	/// Prepara un sprite 8x8 u 8x16 de overflow como BOB de 4 planos. Empaqueta la hoja
	/// par `[imagen][guarda][mascara][guarda]` por fila y plano (ventana Amiga de 32 px).
	/// Los colores se aproximan al indice de BG mas cercano porque el BOB vive en COLOR00-15.
	void build_bob_tile(const OamSprite& o) {
		if (m_bob_count >= kMaxBobs || !m_bob_data.valid()) return;
		eng::u8 px[16u * 16u] {};
		eng::u8 mask[16u * 16u] {};
		const eng::u8 sub = static_cast<eng::u8>(o.attr & 3u);
		const bool flip_h = (o.attr & 0x40u) != 0u;
		for (eng::u16 line = 0; line < (o.merged ? 16u : m_sprite_height); ++line) {
			const SpriteRow sr = sprite_row(o, line);
			eng::u16 a = sr.chr[sr.row];
			eng::u16 b = sr.chr[8u + sr.row];
			if (flip_h) {
				eng::u16 fa = 0u, fb = 0u;
				for (eng::u8 k = 0u; k < 8u; ++k) {
					fa = static_cast<eng::u16>(fa | (((a >> k) & 1u) << (7u - k)));
					fb = static_cast<eng::u16>(fb | (((b >> k) & 1u) << (7u - k)));
				}
				a = fa;
				b = fb;
			}
			for (eng::u8 col = 0u; col < 8u; ++col) {
				const eng::u8 bit = static_cast<eng::u8>(
					((a >> (7u - col)) & 1u) | (((b >> (7u - col)) & 1u) << 1u));
				if (bit == 0u) continue;
				const eng::u32 o = static_cast<eng::u32>(line) * 16u + col;
				px[o] = m_spr_to_bg[sub * 4u + bit];
				mask[o] = 1u;
			}
		}
		eng::u16* const sheet = reinterpret_cast<eng::u16*>(
			m_bob_data.view.data() + static_cast<eng::u32>(m_bob_count) * kBobBytes);
		eng::graphics::pack_interleaved_pair<16u, 16u, 4u>(px, mask, sheet);
		m_bob_x[m_bob_count] = o.ox;
		m_bob_h[m_bob_count] = o.h;
		m_bob_y[m_bob_count] = o.oy;
		++m_bob_count;
	}

	/// Dibuja los BOB de overflow en el plan del frame, sobre el anillo del BG. El Blitter
	/// los pinta despues de los blits de tiles; los sprites HW siguen por Copper encima.
	void draw_bobs(eng::graphics::FramePlan& plan) {
		g_dk_spr_bobs = m_bob_count;
		if (m_bob_count == 0u || !m_bob_data.valid()) return;
		const auto hv = scene.bg().hardware_view();
		const auto target = eng::playfield::make_actor_target(hv);
		const eng::u32 ring_w = static_cast<eng::u32>(hv.bitmap_bytes_per_row) * 8u;
		for (eng::u8 i = 0u; i < m_bob_count; ++i) {
			if (m_bob_x[i] >= hv.viewport_w) continue; // fuera de la ventana visible
			eng::s32 x = static_cast<eng::s32>(hv.mapposx) + static_cast<eng::s32>(m_bob_x[i]);
			x %= static_cast<eng::s32>(ring_w);
			if (x < 0) x += static_cast<eng::s32>(ring_w);
			if ((static_cast<eng::u32>(x) & ~15u) + 32u > ring_w) continue; // costura
			eng::graphics::Bob bob {};
			bob.sheet = eng::ChipView<eng::BobTag> {
				eng::Address<eng::MemoryKind::Chip>::from_storage(
					m_bob_data.view.data() +
					static_cast<eng::u32>(i) * kBobBytes),
				kBobBytes};
			bob.width = 32u;
			bob.height = m_bob_h[i];
			bob.planes = 4u;
			bob.frame_count = 1u;
			bob.layout = eng::graphics::BobLayout::Interleaved;
			bob.draw = eng::graphics::BobDraw::CookieCut;
			bob.mask_pack = eng::graphics::BobMaskPack::InterleavedPair;
			const eng::s16 y = static_cast<eng::s16>(m_bob_y[i] + hv.display_offset);
			(void)eng::graphics::bob_draw(plan, bob, 0u, static_cast<eng::s16>(x), y,
						      target);
		}
	}

	/// Empaqueta un sprite 8x8 u 8x16 como DATA DMA y publica su placement. `count` es el
	/// indice de la instancia en el bloque Chip y avanza con cada sprite hardware.
	void emit_hw_sprite(const OamSprite& o, eng::u8 ch, eng::u16& count) {
		const eng::u16 top = static_cast<eng::u16>(o.oy + kSpriteTop);
		const eng::u16* const inst = sprite_instance(o);
		if (inst == nullptr) return;

		eng::graphics::HwSpritePlacement& p = m_placements[count];
		p = {};
		p.channel = ch;
		// El playfield NES (256 px) va centrado en un DIW de 320 px: origen x = 32.
		// HSTART de Amiga: pixel = hpos - 0x81 -> hpos = 0x81 + 32 + ox.
		p.hpos = static_cast<eng::u16>(0x81u + 32u + o.ox);
		p.vstart = top;
		p.height = o.h;
		p.width_words = 1u;
		p.attach = false;
		// Vista tipada en Chip (el sprite DMA solo lee Chip): frontera explícita.
		p.data = eng::ChipView<eng::SpriteTag> {
			eng::Address<eng::MemoryKind::Chip>::from_storage(
				reinterpret_cast<const eng::u8*>(inst)),
			static_cast<eng::u32>(o.h * 2u + 2u) * 2u};
		m_placement_oam[count] = o.index;
		++count;
	}

	// Refresca los sprites HW desde la OAM con multiplexado vertical. Cada sprite NES queda en el
	// PAR de su subpaleta (attr&3), porque el par comparte COLOR16+s*4; el canal se rearma en el
	// vstart de la siguiente franja. DATA: 8x8 DAT/DATB + terminador (D=1, MSB=izquierda).
	void build_sprites() {
		m_pre_hits_frame = 0u;
		m_bob_count = 0u;
		++m_frame_stamp;
		m_sprite_tall = (n2a_ppu_ctrl() & 0x20u) != 0u;
		m_sprite_height = m_sprite_tall ? 16u : 8u;
		m_sprite_instance_words = static_cast<eng::u16>(m_sprite_height * 2u + 2u);

		OamSprite sprites[64];
		eng::u16 n = 0u;
		for (eng::u8 i = 0; i < 64u; ++i) {
			const eng::u8* const e = &oam[static_cast<eng::u32>(i) * 4u];
			if (e[0] >= 0xF0u) {
				continue;
			}
			OamSprite& s = sprites[n++];
			s.oy = e[0];
			s.tile = e[1];
			s.attr = e[2];
			s.ox = e[3];
			s.tile2 = 0u; s.h = 8u; s.merged = false;
			s.index = i;
		}
		// Fusion vertical de dos OAM 8x8 contiguas (misma X, paleta y flips) en una sola
		// entrada de 16 lineas: la mitad de placements para objetos 16x16. Sprite 0 se
		// mantiene aparte para no alterar su hit.
		if (!m_sprite_tall) {
			bool used[64] {};
			for (eng::u16 i = 0u; i < n; ++i) {
				if (used[i] || sprites[i].index == 0u) continue;
				for (eng::u16 j = static_cast<eng::u16>(i + 1u); j < n; ++j) {
					if (used[j] || sprites[j].index == 0u) continue;
					if (sprites[j].oy == static_cast<eng::u8>(sprites[i].oy + 8u) &&
					    sprites[j].ox == sprites[i].ox &&
					    (sprites[j].attr & 0xC3u) == (sprites[i].attr & 0xC3u)) {
						sprites[i].tile2 = sprites[j].tile;
						sprites[i].h = 16u;
						sprites[i].merged = true;
						used[j] = true;
						break;
					}
				}
			}
			eng::u16 m = 0u;
			for (eng::u16 i = 0u; i < n; ++i) if (!used[i]) sprites[m++] = sprites[i];
			n = m;
		}


		// Insercion por Y: con 9-14 sprites activos es mas barata que un counting sort
		// de 240 cubos (el cubo se ponia a cero cada frame).
		for (eng::u16 i = 1u; i < n; ++i) {
			const OamSprite key = sprites[i];
			eng::u16 j = i;
			while (j > 0u && sprites[j - 1u].oy > key.oy) {
				sprites[j] = sprites[j - 1u];
				--j;
			}
			sprites[j] = key;
		}



		// Fin (exclusivo) del ultimo sprite del canal. Los objetos NES se tocan
		// verticalmente (8x8 consecutivos), asi que el siguiente puede empezar justo en
		// el VSTOP del anterior: es la reutilizacion del haz vertical del canal.
		eng::u16 end_line[kSpriteChannels] {};
		eng::u16 count = 0u;
		eng::u16 overflow = 0u;

		// **Sprite 0 siempre en hardware**: si cae a BOB se pierde su hit y DK se cuelga.
		// Se le reserva un canal de su par antes de repartir el resto; los demas usan el
		// otro canal o degradan a BOB.
		bool has_sprite0 = false;
		for (eng::u16 i = 0u; i < n; ++i) {
			const OamSprite& o = sprites[i];
			if (o.index != 0u) continue;
			const eng::u8 ch = static_cast<eng::u8>((o.attr & 3u) * 2u);
			const eng::u16 top = static_cast<eng::u16>(o.oy + kSpriteTop);
			end_line[ch] = static_cast<eng::u16>(top + o.h);
			emit_hw_sprite(o, ch, count);
			has_sprite0 = true;
			break;
		}

		for (eng::u16 i = 0u; i < n; ++i) {
			const OamSprite& o = sprites[i];
			if (has_sprite0 && o.index == 0u) continue; // ya reservado
			const eng::u16 top = static_cast<eng::u16>(o.oy + kSpriteTop);
			const eng::u8 pair = static_cast<eng::u8>(o.attr & 3u);
			eng::u8 ch = 0xFFu;
			for (eng::u8 c = static_cast<eng::u8>(pair * 2u);
			     c < static_cast<eng::u8>(pair * 2u + 2u); ++c) {
				if (end_line[c] <= top) { ch = c; break; }
			}
			if (ch == 0xFFu) {
				++overflow;
				build_bob_tile(o);
				continue;
			}
			end_line[ch] = static_cast<eng::u16>(top + o.h);
			emit_hw_sprite(o, ch, count);
		}

		// El emisor de placements exige orden no decreciente por vstart; sprite 0 se
		// genero primero y puede tener un vstart mayor que el resto.
		for (eng::u16 i = 1u; i < count; ++i) {
			const eng::graphics::HwSpritePlacement key = m_placements[i];
			const eng::u8 key_oam = m_placement_oam[i];
			eng::u16 j = i;
			while (j > 0u && m_placements[j - 1u].vstart > key.vstart) {
				m_placements[j] = m_placements[j - 1u];
				m_placement_oam[j] = m_placement_oam[j - 1u];
				--j;
			}
			m_placements[j] = key;
			m_placement_oam[j] = key_oam;
		}

		// Prioridad sprite-sprite: dentro de un par, el indice OAM menor debe ocupar el
		// canal menor (SPR2s gana a SPR2s+1). Solo afecta a placements que se solapan.
		for (eng::u8 p = 0u; p < 4u; ++p) {
			const eng::u8 c0 = static_cast<eng::u8>(p * 2u);
			const eng::u8 c1 = static_cast<eng::u8>(c0 + 1u);
			for (eng::u16 i = 0u; i < count; ++i) {
				if (m_placements[i].channel != c1) continue;
				for (eng::u16 j = 0u; j < count; ++j) {
					if (m_placements[j].channel != c0) continue;
					const bool overlap =
						m_placements[i].vstart < m_placements[j].vstart +
										m_placements[j].height &&
						m_placements[j].vstart < m_placements[i].vstart +
										m_placements[i].height;
					if (overlap && m_placement_oam[i] < m_placement_oam[j]) {
						m_placements[i].channel = c0;
						m_placements[j].channel = c1;
						break;
					}
				}
			}
		}

		m_placement_count = count;
		g_dk_spr_placements = count;
		g_dk_spr_overflow = overflow;
	}

	/// Reserva 3 formas de onda (square, triangle, noise) en Chip para el puente APU->Paula.
	void init_audio(eng::amiga::AmigaBackend& backend) {
		m_audio_buf = backend.memory_manager().chip().reserve<eng::AudioTag>(96u, 4u);
		if (!m_audio_buf.valid()) return;
		eng::u8* const d = m_audio_buf.view.data();
		for (eng::u8 i = 0; i < 32u; ++i) {
			// Square 50% (32 B = 16 words por ciclo).
			d[i] = static_cast<eng::u8>(i < 16u ? 0x60u : 0xA0u);
			// Triangle rampa arriba/abajo.
			d[32u + i] = static_cast<eng::u8>(i < 16u ? (i * 16u) : (255u - (i - 16u) * 16u));
			// Noise: LFSR de 15 bits, muestra pseudoaleatoria.
			static eng::u16 lfsr = 1u;
			const eng::u16 bit = static_cast<eng::u16>((lfsr ^ (lfsr >> 1u)) & 1u);
			lfsr = static_cast<eng::u16>((lfsr >> 1u) | (bit << 14u));
			d[64u + i] = static_cast<eng::u8>((lfsr & 1u) ? 0x70u : 0x90u);
		}
	}

	/// Actualiza los 4 canales de Paula desde los registros APU capturados por el HAL.
	/// Modelo simple: pulso/triángulo/ruido por periodo+volumen; sin envelopes ni DMC.
	void update_audio(eng::amiga::AmigaBackend& backend) {
		if (!m_audio_buf.valid()) return;
		const eng::u8* const d = m_audio_buf.view.data();
		const auto period = [](eng::u32 t) {
			eng::u32 p = (t + 1u) * 2u;
			if (p < 124u) p = 124u;
			if (p > 60000u) p = 60000u;
			return static_cast<eng::u16>(p);
		};
		const eng::u8 en = n2a_apu_reg(0x15u);
		const eng::u16 t0 = static_cast<eng::u16>(n2a_apu_reg(0u) | ((n2a_apu_reg(1u) & 7u) << 8u));
		const eng::u16 t1 = static_cast<eng::u16>(n2a_apu_reg(4u) | ((n2a_apu_reg(5u) & 7u) << 8u));
		const eng::u16 t2 = static_cast<eng::u16>(n2a_apu_reg(0x0Au) | ((n2a_apu_reg(0x0Bu) & 7u) << 8u));
		const eng::u8 v0 = static_cast<eng::u8>(((n2a_apu_reg(0u) >> 4u) & 0x0Fu) * 4u);
		const eng::u8 v1 = static_cast<eng::u8>(((n2a_apu_reg(4u) >> 4u) & 0x0Fu) * 4u);
		const eng::u8 vn = static_cast<eng::u8>((n2a_apu_reg(0x0Cu) & 0x0Fu) * 4u);
		static constexpr eng::u16 kNoisePeriod[16] = {4u, 8u, 16u, 32u, 64u, 96u, 128u, 160u,
							      202u, 254u, 380u, 508u, 762u, 1016u, 2034u, 4068u};
		const eng::u16 tn = kNoisePeriod[n2a_apu_reg(0x0Eu) & 0x0Fu];
		if ((en & 1u) != 0u && v0 != 0u) backend.start_audio_buffer(0u, d, 16u, period(t0), v0);
		else backend.stop_audio_channel(0u);
		if ((en & 2u) != 0u && v1 != 0u) backend.start_audio_buffer(1u, d, 16u, period(t1), v1);
		else backend.stop_audio_channel(1u);
		if ((en & 4u) != 0u) backend.start_audio_buffer(2u, d + 32u, 16u, period(t2), 48u);
		else backend.stop_audio_channel(2u);
		if ((en & 8u) != 0u && vn != 0u) backend.start_audio_buffer(3u, d + 64u, 16u, period(tn), vn);
		else backend.stop_audio_channel(3u);
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({384u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E001u);
			return;
		}
		init_audio(backend);
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
		update_sprite_palette_map();
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
		m_bob_data = backend.memory_manager().chip().reserve<eng::BobTag>(
			static_cast<eng::u32>(kMaxBobs) * kBobBytes, 16u);
		m_sprite_cache = backend.memory_manager().chip().reserve<eng::SpriteTag>(
			kSpriteCacheBytes, 16u);
		if (!m_sprite_cache.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E014u);
			return;
		}
		for (eng::u16 i = 0u; i < kSpriteCacheSlots; ++i) {
			m_cache_key[i] = 0xFFFFFFFFu;
			m_cache_stamp[i] = 0u;
		}
		m_sprite_pre = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpritePreBytes, 16u);
		m_pre_ok = m_sprite_pre.valid();
		if (m_pre_ok) {
			m_sprite_tall = false; m_sprite_height = 8u; m_sprite_instance_words = kSprite8x8InstanceWords;
			eng::u16* const base = reinterpret_cast<eng::u16*>(m_sprite_pre.view.data());
			for (eng::u16 tile = 0u; tile < 256u; ++tile) for (eng::u8 f = 0u; f < 4u; ++f) {
				OamSprite o {}; o.tile = static_cast<eng::u8>(tile);
				o.attr = static_cast<eng::u8>((f & 1u) ? 0x40u : 0u) | static_cast<eng::u8>((f & 2u) ? 0x80u : 0u);
				write_instance(base + (static_cast<eng::u32>(tile) * 4u + f) * kSprite8x8InstanceWords, o);
			}
		}
		m_sprite_pre16 = backend.memory_manager().chip().reserve<eng::SpriteTag>(kSpritePre16Bytes, 16u);
		m_pre16_ok = m_sprite_pre16.valid();
		if (m_pre16_ok) {
			m_sprite_tall = true; m_sprite_height = 16u; m_sprite_instance_words = kSpriteMaxInstanceWords;
			eng::u16* const base = reinterpret_cast<eng::u16*>(m_sprite_pre16.view.data());
			for (eng::u16 tile = 0u; tile < 256u; ++tile) for (eng::u8 f = 0u; f < 4u; ++f) {
				OamSprite o {}; o.tile = static_cast<eng::u8>(tile);
				o.attr = static_cast<eng::u8>((f & 1u) ? 0x40u : 0u) | static_cast<eng::u8>((f & 2u) ? 0x80u : 0u);
				write_instance(base + (static_cast<eng::u32>(tile) * 4u + f) * kSpriteMaxInstanceWords, o);
			}
		}

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
		cfg.sprite_data_bytes = 0u; // placements externos: DATA en la cache, no en SpriteManager
		cfg.sprites_armed = false; // placements externos: rearmes verticales por vstart
		cfg.copper_bytes = kSpritesOn ? 4096u : 1536u;

		if (!scene.begin(backend.memory_manager(), cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E002u);
			return;
		}
		scene.bg().set_camera(0, 0);
		if (kSpritesOn) { build_sprites(); } // canales HW desde la OAM (DATA reservada por begin)
		scene.set_sprite_placements(eng::Span<const eng::graphics::HwSpritePlacement>{
			m_placements, m_placement_count});
		plan.clear();
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E003u);
			return;
		}
		if (kSpritesOn && m_bob_count != 0u) {
			draw_bobs(plan);
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x0000E013u);
				return;
			}
			plan.clear();
		}
		// Drenar las celdas sucias acumuladas durante el warmup: la pantalla inicial YA se ha
		// volcado con `scene.fill`; evitar que el primer `update` las reprocese (serian >128 jobs).
		{
			eng::u16 da = 0u; eng::u8 dv = 0u;
			while (n2a_ppu_dirty_pop(&da, &dv)) {}
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
		if (!ready) return;
		++g_dk_updates;
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
			if (kAutoStart && m_frames >= 30u && m_frames < 36u) { pad = static_cast<eng::u8>(pad | 0x08u); } // Start
			n2a_set_pad(pad);
			g_dk_pad = pad;
			g_dk_nmi = n2a_nmi_fires();
		}
		// Logica del port (dirigida por frame).
		if (kRunPort) { n2a_frame(); }
		if (kAudioOn) update_audio(backend);
		// PALETA: si la paleta NES ($3F00-1F) cambio (p. ej. el juego cambia al arrancar la
		// partida), re-mapear los colores Amiga. Si no, el render usa la paleta del titulo
		// (colores equivocados en gameplay). Se re-hornea `g_palette` (el compositor la emite).
		{
			eng::u32 ph = 0u;
			for (eng::u8 i = 0; i < 0x20u; ++i) { ph = ph * 131u + n2a_ppu_pal(i); }
			if (ph != m_pal_hash) {
				m_pal_hash = ph;
				for (eng::u16 i = 0; i < 16u; ++i) {
					g_palette[i] = nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(i)));
					g_palette[16u + i] = nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(0x10u + i)));
				}
				update_sprite_palette_map();
			}
		}
		++m_frames;
		// REBUILD dinamico: si el juego cambio el nametable (transicion de pantalla), recomponer
		// el mapa de metatiles + el banco y re-blitear el anillo. Deteccion: celdas sucias del PPU.
		// REBUILD dinamico INCREMENTAL: consumir las celdas sucias del PPU y recomputar solo los
		// metatiles afectados (antes: rebuild completo 16x15 + dedupe O(n^2) cada frame).
		plan.clear();
		const eng::u16 mt_before = g_mt_count;
		if (update_world_from_dirty()) {
			if (g_mt_count > static_cast<eng::u16>(kMaxMt - 32u)) {
				// Caso raro: tabla incremental demasiado grande o DEMASIADAS celdas cambiadas en
				// un frame (el plan tendria >kMaxBlitJobs=128 jobs) -> rebuild + fill completo.
				rebuild_world_from_port();
				fill_bank(m_bank.view.data(), g_mt_count, kPlanes);
				if (!scene.fill(backend, plan)) {
					ready = false;
					eng::debug::mark_failed(g_eng_run_status, 0x0000E012u);
					return;
				}
			} else {
				// Camino caliente: rellena el banco SOLO de los metatiles NUEVOS y re-blitea
				// SOLO las celdas cambiadas (1 TileBlockCopy interleaved por celda), sin fill.
				for (eng::u16 m = mt_before; m < g_mt_count; ++m) {
					fill_bank_one(m_bank.view.data(), m, kPlanes);
				}
				const eng::u16 bpl = scene.bg().block_planes_lines();
				eng::u16 k = 0u;
				while (k < g_chg_n) {
					plan.clear();
					eng::u16 n = 0u;
					for (; k < g_chg_n && n < 48u; ++k, ++n) {
						if (!scene.bg().add_draw(plan,
							static_cast<eng::u16>(g_chg_tx[k] * kTileW),
							static_cast<eng::u16>(g_chg_ty[k] * bpl),
							g_chg_tx[k], g_chg_ty[k])) {
							break;
						}
					}
					if (n != 0u && !backend.execute_frame_plan(plan)) {
						ready = false;
						eng::debug::mark_failed(g_eng_run_status, 0x0000E012u);
						return;
					}
					if (n == 0u) break;
				}
				plan.clear();
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
		scene.set_sprite_placements(eng::Span<const eng::graphics::HwSpritePlacement>{
			m_placements, m_placement_count});
		if (kSpritesOn) { draw_bobs(plan); } // BOB de overflow, encima del BG
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
		g_eng_frame_telemetry.frame = g_dk_updates;
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
