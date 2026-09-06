#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/core/sinetable.hpp>
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
//     Fields"), cuantizado a 8 colores (3 planos). Mapa FINITO 40x40.
//   - FG (PF1, planos de HW 1,3,5): plaquettes decorativas, 7 colores + índice 0
//     transparente (deja ver el BG a través). Mapa finito 48x40.
// RECORRIDO (visualiza todo el mapa): fases LINEALES (H hasta el borde derecho,
// V hasta el borde inferior, diagonal arriba-izquierda a (0,0)) y después
// LISSAJOUS curvo indefinido. El FG de plaquettes lleva el X a media velocidad
// del BG (parallax 2:1) y comparte la Y: ambos siguen la curva (elipse).
// Todo el scroll fino es hardware (BPLCON1 + BPLxPT vía Copper); el Blitter solo
// pinta la columna/fila entrante en cada cruce de 16 px: CPU mínima a 50 fps.
// display LINEAL (mirror) para que el recorrido vertical no dependa del split.
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

// Recorrido de la cámara del BG (el mapa real 40x40 = 640x640 px):
//   1) LINEAL derecha  hasta el borde derecho  (se ve la fila superior),
//   2) LINEAL abajo     hasta el borde inferior (se ve la columna derecha),
//   3) LINEAL diagonal  arriba-izquierda hasta (0,0) (cruza y ve todo el mapa),
//   4) hacia el centro  y, a partir de ahí,
//   5) LISSAJOUS curvo indefinido (AMBOS playfields siguen curvas: el FG de
//      plaquettes lleva el X a media velocidad del BG → elipse sobre la curva).
constexpr eng::s32 kBgMaxX = static_cast<eng::s32>(kBgCols) * static_cast<eng::s32>(kTileW) - static_cast<eng::s32>(kViewportW);
constexpr eng::s32 kBgMaxY = static_cast<eng::s32>(kBgRows) * static_cast<eng::s32>(kTileH) - static_cast<eng::s32>(kViewportH);
constexpr eng::s32 kCenterX = kBgMaxX / 2;   // 160
constexpr eng::s32 kCenterY = kBgMaxY / 2;   // 192
constexpr eng::s32 kRadiusX = 100;           // órbita Lissajous contenida en el mapa
constexpr eng::s32 kRadiusY = 140;

enum class TourPhase : eng::u8 { HToEnd = 0, VToEnd, ObToOrigin, ToCenter, Lissajous };

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	TourPhase m_phase = TourPhase::HToEnd;
	eng::u32 m_frameOfDay = 0;
	bool ready = false;

	static constexpr eng::SineTable<255, 128> kSin {};

	// Paso hacia `target` (≤ max_step px por eje) manteniendo 1-2 px/frame
	// constantes (scroll suave); devuelve el avance a aplicar en este eje.
	static eng::s32 step_toward(eng::s32 cur, eng::s32 target, eng::s32 maxStep) {
		if (cur == target) return 0;
		const eng::s32 d = target > cur ? 1 : -1;
		const eng::s32 mag = target > cur ? (target - cur) : (cur - target);
		return d * (mag < maxStep ? mag : maxStep);
	}

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::reset(g_eng_frame_telemetry);
		// Arena Chip: dos anillos 3p con MIRROR (linear_display) + copper.
		if (!backend.configure_memory({260u * 1024u, 16u * 1024u, 8u * 1024u})) {
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
		// display LINEAL (espejo del bucle, sin split): con viewport 256 el split
		// del corkscrew caería en raster 256..296 (no esperable con el comparador
		// de 8 bits) al recorrer en vertical; el espejo lo evita a costa de 2×
		// blits por tile (el recorrido usa pasos ≤2, cabe holgado en 50 fps).
		scene_cfg.linear_display = true;
		scene_cfg.max_step = 2;
		// Igual que la 201: el hardware oculta la columna/fila de guarda (16 px),
		// así que con bias 1,1 el offset visible (0,0) muestra map[0][0].
		scene_cfg.visible_tile_bias_x = 1;
		scene_cfg.visible_tile_bias_y = 1;

		// FG (PF1, delante): plaquettes transparentes. Mapa FINITO (wrap 0):
		// es decoración, el recorrido del BG no lo saca de sus límites.
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(kFgMap, kFgCols * kFgRows);
		scene_cfg.map.width = kFgCols;
		scene_cfg.map.height = kFgRows;
		scene_cfg.map.edge_tile = 0; // fuera de límites: transparente
		// BG (PF2, detrás): el mundo real a 8 colores, FINITO 40x40 (el recorrido
		// va de borde a borde para visualizar todo el mapa).
		scene_cfg.map2.cells = eng::Span<const eng::u16>::from_raw(kBgMap, kBgCols * kBgRows);
		scene_cfg.map2.width = kBgCols;
		scene_cfg.map2.height = kBgRows;
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
		// El FG necesita paso 2 en Y (comparte el recorrido vertical del BG) pero
		// solo ~1 en X (parallax 2:1). update_scroll clampa por eje a max_step.
		scene.fg().set_scroll_step(2);   // BG visual (PF2, el mapa real)
		scene.bg().set_scroll_step(2);   // FG visual (PF1, plaquettes)

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

		// Posición actual de la cámara del mapa (el BG visual es PF2 = `fg()`).
		const eng::s32 bgX = scene.fg().mapposx();
		const eng::s32 bgY = scene.fg().mapposy();

		// Target del BG según la fase del recorrido.
		eng::s32 tX = 0, tY = 0;
		switch (m_phase) {
			case TourPhase::HToEnd: tX = kBgMaxX; tY = 0; break;
			case TourPhase::VToEnd: tX = kBgMaxX; tY = kBgMaxY; break;
			case TourPhase::ObToOrigin: tX = 0; tY = 0; break;
			case TourPhase::ToCenter: tX = kCenterX; tY = kCenterY; break;
			case TourPhase::Lissajous: {
				const eng::u32 f = m_frameOfDay;
				const eng::u8 a = static_cast<eng::u8>((f * 2u) & 127u);
				const eng::u8 b = static_cast<eng::u8>((f * 3u) & 127u);
				tX = kCenterX + (kSin[a] * kRadiusX) / 255;
				tY = kCenterY + (kSin[b] * kRadiusY) / 255;
				break;
			}
		}
		if (m_phase != TourPhase::Lissajous && bgX == tX && bgY == tY) {
			m_phase = static_cast<TourPhase>(static_cast<eng::u8>(m_phase) + 1u);
		}
		// Movimiento por campo INDEPENDIENTE:
		//   BG (mapa): recorre las fases a ≤2 px/frame por eje.
		//   FG (plaquettes): X = BG/2 (parallax 2:1, curva en elipse) y Y = BG
		//     (comparte el recorrido vertical). Ambos scroll HW (BPLCON1/BPLxPT
		//     vía Copper); el Blitter solo pinta la columna/fila entrante.
		const eng::s32 dxBg = step_toward(bgX, tX, 2);
		const eng::s32 dyBg = step_toward(bgY, tY, 2);
		const eng::s32 fgX = scene.bg().mapposx();
		const eng::s32 fgY = scene.bg().mapposy();
		const eng::s32 tFgX = bgX / 2;
		const eng::s32 dxFg = step_toward(fgX, tFgX, 1);
		const eng::s32 dyFg = step_toward(fgY, bgY, 2);

		bool ok = scene.fg().update_scroll(plan, dxBg, dyBg);   // BG visual (PF2)
		if (ok) ok = scene.bg().update_scroll(plan, dxFg, dyFg); // FG visual (PF1)
		if (!ok) { /* borde de mapa sin recorrido: no avanza este frame */ }

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
		tel.fillup_extra = static_cast<eng::u16>(bgX & 0xffffu);
		// DEBUG TEMPORAL: fase + posición del BG en el detail (leer al final).
		g_eng_run_status.detail = (static_cast<eng::u32>(m_phase) << 24) |
			((static_cast<eng::u32>(bgY) & 0x3ffu) << 12) |
			(static_cast<eng::u32>(bgX) & 0x3ffu);
		++m_frameOfDay;
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
