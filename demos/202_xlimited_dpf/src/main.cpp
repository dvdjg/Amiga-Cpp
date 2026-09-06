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
// Inicializador no-cero: fuerza el símbolo a .data (no .bss) para que el runner
// resuelva su dirección runtime igual que g_eng_run_status (el mapeo de
// secciones del canal lateral no es 1:1 en .bss).
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry = { 0xFFFFFFFFu, 0, 0, 0, 0, {0, 0, 0} };
}

// Mapa real "Beginning Fields" a 8 colores (BG / PF2) + plaquettes (FG / PF1):
// paletas, mapas y conteos generados por tools/demo202/emit-202.mjs.
#include "../../../out/demo202/const_202.h"

// Bancos X-Limited interleaved YA listos (incbin en .MEMF_CHIP → hunk HUNKF_CHIP).
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
//   - BG (PF2, planos HW 2,4,6): el mundo real de la 201 a 8 colores (3 planos).
//   - FG (PF1, planos HW 1,3,5): plaquettes, 7 colores + índice 0 transparente.
//
// Viewport RECORTADO a 320×208 (13 filas): el split vertical del corkscrew cae
// siempre en raster ≤ 248 (comparador de 8 bits) → NO hace falta linear_display.
// Anillo = display_height = 208 + 2·16 = 240.
//
// MAPAS SIEMPRE TOROIDALES (wrap): el scroll es un único algoritmo de bucle
// vertical/horizontal (sin modos de borde). El recorrido se limita a un
// PASO COMPLETO por el mundo (320 px en X, 432 en Y) para ver cada columna/fila
// una vez; al estar dentro del primer paso del toro, la costura nunca se ve.
//
// RECORRIDO (visualiza todo el mapa real):
//   1) LINEAL derecha (offset 1 px/frame)  → x = kBgMaxX (320),
//   2) LINEAL abajo   (offset 1 px/frame)  → y = kBgMaxY (432),
//   3) diagonal arriba-izquierda a (0,0),
//   4) hacia el centro,
//   5) LISSAJOUS con la amplitud COMPLETA del mundo (x∈[0,320], y∈[0,432]) que
//      desplaza todo el mapa de un lado a otro; el salto por frame ≤ 2 px.
//
// El FG de plaquettes está DESACOPLADO del BG: barre su propio mundo en X de un
// lado a otro (patrulla 0..160 a 1 px/frame) de forma continua, y solo comparte
// la Y con el BG porque el compositor DPF usa un único split de Copper.
// -----------------------------------------------------------------------------

constexpr eng::u32 kViewportW = 320;
constexpr eng::u32 kViewportH = 208;      // recorte: 13 filas (split canónico ≤215)
constexpr eng::u32 kTileW = 16;
constexpr eng::u32 kTileH = 16;
constexpr eng::u8  kPlanes = 3;           // planos POR playfield (DPF 3+3 = 6 HW)
constexpr eng::u32 kDisplayH = kViewportH + 2u * kTileH; // anillo del corkscrew 240

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

// Límites del recorrido sobre el mundo toroidal (visible al completo, sin
// mostrar la costura): última columna/fila completamente visibles en pantalla.
constexpr eng::s32 kBgMaxX = static_cast<eng::s32>(kBgCols) * static_cast<eng::s32>(kTileW) - static_cast<eng::s32>(kViewportW);
constexpr eng::s32 kBgMaxY = static_cast<eng::s32>(kBgRows) * static_cast<eng::s32>(kTileH) - static_cast<eng::s32>(kViewportH);
// Centro/radio de la órbita Lissajous del BG: amplitud COMPLETA del mundo
// (x∈[0,320], y∈[0,432]) para que las curvas desplacen todo el mapa de un lado
// a otro. Los índices avanzan LENTO (cada 2-3 frames) para que el salto por
// frame sea ≤ 2 px (si el target variara 1 índice/frame, ~4-6 px/frame).
constexpr eng::s32 kCx = kBgMaxX / 2;     // 160
constexpr eng::s32 kCy = kBgMaxY / 2;     // 216
constexpr eng::s32 kRx = kBgMaxX / 2;     // 160 → x ∈ [0, 320]
constexpr eng::s32 kRy = kBgMaxY / 2;     // 216 → y ∈ [0, 432]
// El FG es INDEPENDIENTE: su propio oscilador en X (centro/radio propios, salto
// ≤2 px/frame) y comparte la Y con el BG (requisito del split único del DPF).
constexpr eng::s32 kFgCx = 96, kFgR = 80; // x ∈ [16, 176]

enum class TourPhase : eng::u8 { HToEnd = 0, VToEnd, ObToOrigin, ToCenter, Lissajous };

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	TourPhase m_phase = TourPhase::HToEnd;
	eng::u32 m_frameOfDay = 0;
	// Fases lentas de los osciladores (índice de la tabla seno de 256 + contador
	// de sub-muestreo para acotar el salto por frame ≤ 2 px).
	eng::u8 m_sx = 0, m_sy = 0, m_sf = 0;
	eng::u8 m_sxAcc = 0, m_syAcc = 0;
	bool ready = false;

	static constexpr eng::SineTable<255, 256> kSin {};

	// Paso hacia `target` (≤ maxStep px por eje): devuelve el avance a aplicar.
	static eng::s32 step_toward(eng::s32 cur, eng::s32 target, eng::s32 maxStep) {
		if (cur == target) return 0;
		const eng::s32 d = target > cur ? 1 : -1;
		const eng::s32 mag = target > cur ? (target - cur) : (cur - target);
		return d * (mag < maxStep ? mag : maxStep);
	}

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		eng::debug::reset(g_eng_frame_telemetry);
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
		scene_cfg.linear_display = false; // viewport 208 → split canónico (sin espejo)
		scene_cfg.max_step = 2;           // tope del engine; la demo limita a 1 px en H/V
		scene_cfg.visible_tile_bias_x = 1;
		scene_cfg.visible_tile_bias_y = 1;

		// FG (PF1, delante): plaquettes transparentes. Mundo TOROIDAL (siempre el
		// mismo algoritmo de scroll; su patrulla en X se mantiene dentro de un
		// primer paso del toro).
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(kFgMap, kFgCols * kFgRows);
		scene_cfg.map.width = kFgCols;
		scene_cfg.map.height = kFgRows;
		scene_cfg.map.wrap_x = kFgCols;
		scene_cfg.map.wrap_y = kFgRows;
		scene_cfg.map.edge_tile = 0;
		// BG (PF2, detrás): mundo real a 8 colores, toroidal.
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
		scene_cfg.dpf.parallax_x = false;           // cada campo se mueve por su cuenta
		scene_cfg.palette = kDpfPalette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020202u);
			return;
		}
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

		// Posición actual de cada campo.
		const eng::s32 bgX = scene.fg().mapposx();   // BG visual (mapa)
		const eng::s32 bgY = scene.fg().mapposy();
		const eng::s32 fgX = scene.bg().mapposx();   // FG visual (plaquettes)
		const eng::s32 fgY = scene.bg().mapposy();

		// Osciladores lentos: el del BG solo avanza en la fase Lissajous; el del
		// FG avanza SIEMPRE (independiente del recorrido del BG). Sub-muestreo
		// para que el desplazamiento por frame sea ≤ 2 px.
		if (m_phase == TourPhase::Lissajous) {
			if (++m_sxAcc >= 2) { m_sxAcc = 0; ++m_sx; }  // X: un índice cada 2 f.
			if (++m_syAcc >= 3) { m_syAcc = 0; ++m_sy; }  // Y: un índice cada 3 f.
		}
		++m_sf;

		// Target del BG según la fase (mapa TOROIDAL: se recorre un primer paso).
		eng::s32 tX = 0, tY = 0;
		switch (m_phase) {
			case TourPhase::HToEnd: tX = kBgMaxX; tY = 0; break;
			case TourPhase::VToEnd: tX = kBgMaxX; tY = kBgMaxY; break;
			case TourPhase::ObToOrigin: tX = 0; tY = 0; break;
			case TourPhase::ToCenter: tX = kCx; tY = kCy; break;
			case TourPhase::Lissajous: {
				// Amplitud COMPLETA del mundo: desplaza todo el mapa de un lado a
				// otro; los índices lentos acotan el salto por frame a ≤ 2 px.
				tX = kCx + (kSin[m_sx] * kRx) / 255;
				tY = kCy + (kSin[m_sy] * kRy) / 255;
				break;
			}
		}
		if (m_phase != TourPhase::Lissajous && bgX == tX && bgY == tY) {
			m_phase = static_cast<TourPhase>(static_cast<eng::u8>(m_phase) + 1u);
		}

		// Los scrolles LINEALES (H/V) usan offset 1 px/frame; el resto ≤2 px/frame.
		const eng::s32 stepLim = (m_phase == TourPhase::HToEnd || m_phase == TourPhase::VToEnd) ? 1 : 2;
		const eng::s32 dxBg = step_toward(bgX, tX, stepLim);
		const eng::s32 dyBg = step_toward(bgY, tY, stepLim);

		// FG DESACOPLADO: su X oscila de forma independiente (8-way: ambos
		// sentidos del eje) y su Y es COMPARTIDA con el BG (único split de Copper
		// del DPF), así que en las fases con movimiento vertical el FG también
		// se mueve arriba/abajo y en diagonal.
		const eng::s32 tFgX = kFgCx + (kSin[m_sf] * kFgR) / 255;
		const eng::s32 dxFg = step_toward(fgX, tFgX, 2);
		const eng::s32 dyFg = step_toward(fgY, tY, stepLim);

		bool ok = scene.fg().update_scroll(plan, dxBg, dyBg);
		if (ok) ok = scene.bg().update_scroll(plan, dxFg, dyFg);

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
