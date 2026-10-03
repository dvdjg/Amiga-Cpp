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
constexpr eng::u16 kViewportW = 256u;
constexpr eng::u16 kViewportH = 256u;
constexpr eng::u8  kPlanes = 4u;
constexpr eng::u16 kDisplayH = 256u;                 // y_mode=Off -> sin split
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
// subpaleta NES (0..3) — el nametable NES no sigue ningun patron TL/TL+1.
eng::u8 g_mt_tile[64][4] {};
eng::u8 g_mt_pal[64] {};
eng::u8 g_mt_used[64] {};

// Genera una fila planar (16 px = 1 word) del metatile: mitad izquierda en bits
// 15..8, mitad derecha en bits 7..0. glyph = indice de metatile mod 16, variant = (idx>>4)&3.
eng::u16 mt_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	const eng::u8 mt = static_cast<eng::u8>((variant << 4u) | (glyph & 15u));
	const eng::u8 pal = g_mt_pal[mt];
	const bool lower = (y >= 8u);
	const eng::u8 row8 = static_cast<eng::u8>(y & 7u);
	// Los 4 tiles NES del metatile: 0=TL, 1=TR, 2=BL, 3=BR.
	const eng::u8 tl = g_mt_tile[mt][lower ? 2u : 0u];
	const eng::u8 tr = g_mt_tile[mt][lower ? 3u : 1u];
	const eng::u8 p0l = CHR_ROM[static_cast<eng::u32>(tl) * 16u + row8];
	const eng::u8 p1l = CHR_ROM[static_cast<eng::u32>(tl) * 16u + 8u + row8];
	const eng::u8 p0r = CHR_ROM[static_cast<eng::u32>(tr) * 16u + row8];
	const eng::u8 p1r = CHR_ROM[static_cast<eng::u32>(tr) * 16u + 8u + row8];
	switch (plane) {
	case 0u: return static_cast<eng::u16>((static_cast<eng::u16>(p0l) << 8u) | p0r);
	case 1u: return static_cast<eng::u16>((static_cast<eng::u16>(p1l) << 8u) | p1r);
	case 2u: return ((pal & 1u) ? 0xFFFFu : 0u);
	default: return ((pal & 2u) ? 0xFFFFu : 0u);
	}
}

eng::u16 g_palette[16] {};

// Reconstruye el mapa de metatiles a partir del nametable NES (tilebase actual):
// por cada metatile (16x15), su tile TL y su subpaleta (del primer cuadrante).
void rebuild_world_from_port() {
	const eng::u16 tb = static_cast<eng::u16>((n2a_ppu_ctrl() & 1u) * 0x400u);
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
			// Dedupe: reutiliza un descriptor existente si (4 tiles, pal) coinciden.
			eng::u8 idx = 0xFFu;
			for (eng::u8 m = 0; m < 64u; ++m) {
				if (!g_mt_used[m]) continue;
				if (g_mt_pal[m] != pal) continue;
				if (g_mt_tile[m][0] == t4[0] && g_mt_tile[m][1] == t4[1] &&
				    g_mt_tile[m][2] == t4[2] && g_mt_tile[m][3] == t4[3]) {
					idx = m;
					break;
				}
			}
			if (idx == 0xFFu) {
				for (eng::u8 m = 0; m < 64u; ++m) {
					if (!g_mt_used[m]) {
						g_mt_tile[m][0] = t4[0]; g_mt_tile[m][1] = t4[1];
						g_mt_tile[m][2] = t4[2]; g_mt_tile[m][3] = t4[3];
						g_mt_pal[m] = pal;
						g_mt_used[m] = 1u;
						idx = m;
						break;
					}
				}
			}
			g_cells[static_cast<eng::u32>(ty) * kMtCols + tx] = (idx == 0xFFu) ? 0xFFFFu : idx;
		}
		// Columnas del anillo mas alla de la pantalla NES: repite la pantalla (toroide),
		// para que las 22 columnas que lee el compositor X-limited esten definidas.
		for (eng::u16 tx = kScreenCols; tx < kMtCols; ++tx) {
			g_cells[static_cast<eng::u32>(ty) * kMtCols + tx] =
				g_cells[static_cast<eng::u32>(ty) * kMtCols + ((tx - kScreenCols) % kScreenCols)];
		}
	}
}

struct DkXlGame {
	playfield::XlimitedScene<kScrollConsts, playfield::TileLayerMap, playfield::ScrollProgressive> scene {};
	playfield::XlimitedSceneConfigT<playfield::TileLayerMap> cfg {};
	eng::graphics::FramePlan plan {};
	eng::s32 cam_x = 0;
	eng::s32 cam_y = 0;
	bool ready = false;
	eng::u16 m_frames = 0u;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({256u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E001u);
			return;
		}
		// Arranca el port unos frames para que escriba el nametable y la paleta.
		for (eng::u16 i = 0; i < 4u; ++i) {
			n2a_frame();
		}
		for (eng::u16 i = 0; i < 16u; ++i) {
			g_palette[i] = nes_to_amiga(n2a_ppu_pal(static_cast<eng::u8>(i)));
		}
		rebuild_world_from_port();

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
		cfg.tileset_count = kTilesetCount;
		cfg.fg_row_fn = &mt_row;
		cfg.bg_row_fn = nullptr;
		cfg.palette = eng::PaletteWords {g_palette, 16u};
		cfg.copper_bytes = 1536u;

		if (!scene.begin(backend.memory_manager(), cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x0000E002u);
			return;
		}
		scene.bg().set_camera(0, 0);
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
		// Logica del port (dirigida por frame).
		n2a_frame();
		++m_frames;
		// Scroll por hardware: la camara del engine avanza (X). El compositor X-limited
		// reemite BPLxPT/BPL1MOD/conBPL1 por Copper.
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 160});
		const bool scrolled = scene.bg().update_scroll(plan, 1, 0);
		if (!scrolled) {
			scene.bg().set_camera(0, 0);
		}
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0000E010u);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0000E011u);
			return;
		}
		g_eng_run_status.detail = 0xE0000000u | ((static_cast<eng::u32>(scene.bg().mapposx()) & 0xffffu) << 8u);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
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
