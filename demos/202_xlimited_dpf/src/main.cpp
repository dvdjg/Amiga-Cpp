#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

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

extern "C" {
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry {};
}

// Mapa real "Beginning Fields" a 8 colores (BG / PF2) + plaquettes (FG / PF1):
// paletas, mapas y conteos generados por tools/demo202/emit-202.mjs.
#include "../../../out/demo202/const_202.h"

// Bancos X-Limited interleaved YA listos (incbin en .MEMF_CHIP → hunk HUNKF_CHIP):
//   - FG (PF1, planos 1,3,5): plaquettes 7 colores + transparencia (1920 B).
//   - BG (PF2, planos 2,4,6): "Beginning Fields" a 8 colores (149760 B).
__asm__(".section bank_fg.MEMF_CHIP, \"aw\"\n"
	".globl g_bank_fg\ng_bank_fg:\n"
	".align 2\n"
	".incbin \"out/demo202/fg/tilebank_xlimited.bin\"\n"
	".globl g_bank_fg_size\ng_bank_fg_size:\n"
	".long . - g_bank_fg");
__asm__(".section bank_bg.MEMF_CHIP, \"aw\"\n"
	".globl g_bank_bg\ng_bank_bg:\n"
	".align 2\n"
	".incbin \"out/demo202/bg/tilebank_xlimited_8c_kmeans.bin\"\n"
	".globl g_bank_bg_size\ng_bank_bg_size:\n"
	".long . - g_bank_bg");
extern "C" const unsigned char g_bank_fg[];
extern "C" const unsigned int g_bank_fg_size;
extern "C" const unsigned char g_bank_bg[];
extern "C" const unsigned int g_bank_bg_size;

namespace {
namespace field = eng::field;

// -----------------------------------------------------------------------------
// Demo 202 — DPF 3+3 (dual playfield) sobre el corkscrew X-Limited.
//
// DOS playfields con banco/mapa/paleta propios, cada uno con su scroll:
//   - BG (PF2, planos de HW 2,4,6): el MISMO mundo real de la 201 ("Beginning
//     Fields"), cuantizado a 8 colores (3 planos). 40x40, toroidal.
//   - FG (PF1, planos de HW 1,3,5): plaquettes decorativas, 7 colores + índice 0
//     transparente (deja ver el BG a través). 48x40, toroidal.
// El movimiento es PARALLAX X: el BG avanza a 2 px/frame y el FG a 1 px/frame
// (cada campo con `set_scroll_step` propio). Todo el scroll fino es hardware
// (BPLCON1 + BPLxPT vía Copper); el Blitter solo pinta la columna entrante en
// cada cruce de 16 px: intervención de CPU mínima a 50 fps.
// -----------------------------------------------------------------------------

constexpr eng::u32 kViewportW = 320;
constexpr eng::u32 kViewportH = 256;      // viewport TOTAL del corkscrew (16 filas)
constexpr eng::u32 kTileW = 16;
constexpr eng::u32 kTileH = 16;
constexpr eng::u8  kPlanes = 3;           // planos POR playfield (DPF 3+3 = 6 HW)
constexpr eng::u32 kDisplayH = kViewportH + 2u * kTileH; // anillo = 288 (NO 208+32)

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/       kTileW,
	/*tile_height=*/      kTileH,
	/*display_height=*/   kDisplayH,
	/*display_planelines=*/kDisplayH * kPlanes,
	/*planes=*/           kPlanes,
};

// Paleta DPF de 16 registros: PF1 (plaquettes, regs 0..7) + PF2 (mapa, regs 8..15).
constexpr eng::u16 kDpfPalette[16] {
	0x000, 0xe44, 0xf86, 0xfe0, 0x4c8, 0x48e, 0xc5f, 0xfdf,
	0x664, 0x5a6, 0xb95, 0xa98, 0x8cd, 0x9d8, 0xeb5, 0xdc9,
};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::reset(g_eng_frame_telemetry);
		// 200 KB de arena Chip: dos anillos 3p (BG+FG) + copper + buffers.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020201u);
			return;
		}

		scene_cfg.viewport_w = static_cast<eng::u16>(kViewportW);
		scene_cfg.viewport_h = static_cast<eng::u16>(kViewportH);
		scene_cfg.tile_width = static_cast<eng::u16>(kTileW);
		scene_cfg.tile_height = static_cast<eng::u16>(kTileH);
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.linear_display = false; // sin scroll Y en esta demo (no hay split)
		scene_cfg.max_step = 2;
		// Igual que la 201: el hardware oculta la columna/fila de guarda (16 px), así
		// que con bias 1,1 el offset visible (0,0) muestra map[0][0] en la esquina.
		scene_cfg.visible_tile_bias_x = 1;
		scene_cfg.visible_tile_bias_y = 1;

		// FG (PF1, delante): plaquettes transparentes, mapa propio toroidal.
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(kFgMap, kFgCols * kFgRows);
		scene_cfg.map.width = kFgCols;
		scene_cfg.map.height = kFgRows;
		scene_cfg.map.wrap_x = kFgCols;
		scene_cfg.map.wrap_y = kFgRows;
		scene_cfg.map.edge_tile = 0;
		// empty_tile por defecto (0xFFFF): el tile 0 (transparente) SÍ se pinta,
		// para que las columnas entrantes del FG se limpien y dejen ver el BG.
		// BG (PF2, detrás): el mundo real a 8 colores, toroidal como la 201.
		scene_cfg.map2.cells = eng::Span<const eng::u16>::from_raw(kBgMap, kBgCols * kBgRows);
		scene_cfg.map2.width = kBgCols;
		scene_cfg.map2.height = kBgRows;
		scene_cfg.map2.wrap_x = kBgCols;
		scene_cfg.map2.wrap_y = kBgRows;
		scene_cfg.map2.edge_tile = 0;

		scene_cfg.tileset_count = kBgTiles > kFgTiles ? kBgTiles : kFgTiles;
		scene_cfg.blocks_prebuilt = g_bank_fg;      // PF1 (plaquettes)
		scene_cfg.blocks_prebuilt_size = g_bank_fg_size;
		scene_cfg.blocks_prebuilt2 = g_bank_bg;     // PF2 (mapa real 8c)
		scene_cfg.blocks_prebuilt2_size = g_bank_bg_size;
		scene_cfg.dpf.enabled = true;               // DPF 3+3 homogéneo
		scene_cfg.dpf.parallax_x = false;           // el parallax lo impone la demo
		scene_cfg.palette = kDpfPalette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020202u);
			return;
		}
		// Parallax X: BG (PF2, `fg()` por rol de campo) a 2 px/frame y FG
		// (PF1, `bg()`) a 1 px/frame. update_scroll descompone en sub-pasos
		// atómicos de 1 px (paint-then-advance).
		scene.fg().set_scroll_step(2);
		scene.bg().set_scroll_step(1);

		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020203u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020204u);
			return;
		}
		scene.install(backend);

		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x00020200u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 120});

		// Movimiento por campo INDEPENDIENTE (parallax X a distinta velocidad).
		// La Y no se mueve en esta demo: solo X (cada campo a su ritmo).
		bool ok = scene.fg().update_scroll(plan, 2, 0);   // BG visual (PF2)
		if (ok) ok = scene.bg().update_scroll(plan, 1, 0); // FG visual (PF1)

		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020210u);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020211u);
			return;
		}

		auto& tel = g_eng_frame_telemetry;
		tel.frame = context.frame.frame_index;
		const eng::u32 w = plan.blit_budget().words;
		tel.blit_jobs = plan.blit_job_count();
		tel.blit_words = static_cast<eng::u16>(w > 0xffffu ? 0xffffu : w);
		tel.copper_words = scene.copper_words();
		tel.fillup_extra = static_cast<eng::u16>(scene.bg().mapposx() & 0xffffu);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) {
			scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
