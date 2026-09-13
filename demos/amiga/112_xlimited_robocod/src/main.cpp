// ============================================================================
// Demo 112 - RoboCod: single playfield de 5 planos (4 FG + 1 BG por blitter)
// ============================================================================
//
// Porte del truco del RoboCod ORIGINAL (sin DPF): **un solo playfield de 5
// planos**. Los 4 primeros (16 colores) son el FG de juego (plataformas); el 5.º
// plano es el FONDO, que scrollea a distinta velocidad dibujandose por Blitter
// con su propio offset (`parallax_plane`). El blit del tilemap FG es interleaved
// y cubre los 5 planos (pisa el plano de fondo con 0); DESPUES, cada frame, se
// re-copia por Blitter la ventana del patron al 5.º plano (tecnica "soft DPF").
// La copia usa el barrel shifter para dar continuidad sub-pixel y deja una guarda
// enmascarada (ver `make_bg_plane_copy_job` y kGuardX).
//
// Paleta de 32 indices MAPEADA a 16: `palette[c]==palette[c+16]` para c=1..15, de
// modo que el plano de fondo NO tinta el FG; solo el indice 0/16 pasa de negro
// (fondo off) al color del patron (fondo on). Asi el fondo, al moverse bajo el FG,
// no se nota en el FG ("duplica la paleta").
//
//   X: Finite (mundo X cabe en el bitmap)   Y: usada (corkscrew)

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#ifdef K_DIAG_CYCLES
#include <eng/debug/peripheral.hpp>
#endif
#include <eng/memory/arena.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/field/tile_demo.hpp>

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

namespace {

namespace field = eng::field;

constexpr eng::u16 kTileW = 16;
constexpr eng::u16 kTileH = 16;
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u16 kDisplayH = 288;        // corkscrew: 256 + 2*16
constexpr eng::u8  kPlanes = 5;            // 4 FG (16 colores) + 1 BG (RoboCod)
constexpr eng::u8  kParallaxPlane = 4;
constexpr eng::u8  kParallaxDiv = 2;

constexpr eng::u16 kMapCols = 25;          // 400 px (X Finite)
constexpr eng::u16 kMapRows = 32;          // 512 px (mayor que la ventana)
constexpr eng::u16 kTilesetCount = 32;
// La copia con shift deja una guarda de hasta 15 px al principio del bitmap del
// fondo (shift-in enmascarado a cero). La cámara nunca baja de 16 px para que esa
// guarda quede fuera de la ventana visible.
constexpr eng::s32 kGuardX = 16;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// Tiles del FG (plataformas). El plano 4 (fondo) lo pinta el engine.
eng::u16 fg_row(eng::u8 glyph, eng::u8, eng::u8 row, eng::u8 plane) {
	if (glyph == 0u) return 0u;                 // transparente
	const eng::u8 c = (row == 0u) ? 6u : 5u;    // borde claro / cuerpo
	return (c & (1u << plane)) != 0u ? 0xffffu : 0u;
}

// Paleta 32 mapeada a 16: 0..15 FG (0 negro); 16 = color del fondo; 17..31 = copia
// de 1..15 (duplicado -> el plano de fondo no tinta el FG).
constexpr eng::u16 kPalette[32] {
	0x000, 0x210, 0x320, 0x430, 0x540, 0xf80, 0xff0, 0xfff,
	0x8af, 0x9cf, 0x0cf, 0x0f9, 0xf40, 0xf00, 0xf0f, 0xaaa,
	0x037, 0x210, 0x320, 0x430, 0x540, 0xf80, 0xff0, 0xfff,
	0x8af, 0x9cf, 0x0cf, 0x0f9, 0xf40, 0xf00, 0xf0f, 0xaaa,
};

// Raster colors (Copper) del fondo: el color del plano 4 (COLOR16) cambia por
// líneas de raster -> gradiente pastel sobre las bandas. Requiere display lineal
// (sin split de Copper) para no desordenar el orden de los WAIT.
constexpr field::RasterColorZone kColorZones[] {
	field::raster_color(41u, 16u, 0x8cfu),
	field::raster_color(73u, 16u, 0x9dfu),
	field::raster_color(105u, 16u, 0xaefu),
	field::raster_color(137u, 16u, 0x9cfu),
	field::raster_color(169u, 16u, 0x8bfu),
	field::raster_color(201u, 16u, 0x7afu),
	field::raster_color(233u, 16u, 0x6afu),
};

eng::u16 g_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::MemoryBlock m_bg_pattern {};
	eng::s16 m_dx = 1, m_dy = 1;
	bool ready = false;

	static constexpr eng::u16 kPatPeriodPx = 512;  // periodo horizontal del motivo
	static constexpr eng::u16 kPatRowBytes = 128;  // 1024 px = 2 periodos (margen para la ventana)
	static constexpr eng::u16 kPatRows = 512;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({300u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011201u);
			return;
		}
		// Mapa tipo plataformas: suelo + plataformas flotantes; resto transparente.
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				eng::u16 t = 0u;
				if (y >= static_cast<eng::u16>(kMapRows - 3u)) t = 1u;
				else if ((y == 8u || y == 16u || y == 22u) && ((x % 10u) < 4u)) t = 1u;
				else if ((y == 12u || y == 26u) && ((x % 14u) < 3u)) t = 2u;
				g_map[static_cast<eng::u32>(y) * kMapCols + x] = t;
			}
		}

		// Patrón de fondo (1 bit) en Chip RAM (fuente del Blitter): bandas
		// diagonales + puntos (motivo de tiles). Dos periodos de 512 px (1024 px =
		// 128 bytes) para que la ventana copiada nunca invada la fila siguiente al
		// desplazarse horizontalmente.
		m_bg_pattern = backend.memory().chip.allocate(
			static_cast<eng::u32>(kPatRowBytes) * kPatRows, 16);
		if (m_bg_pattern.valid()) {
			auto* p = static_cast<eng::u8*>(m_bg_pattern.data);
			for (eng::u16 y = 0; y < kPatRows; ++y) {
				for (eng::u16 bx = 0; bx < kPatRowBytes; ++bx) {
					eng::u8 v = 0u;
					for (eng::u8 bit = 0; bit < 8u; ++bit) {
						const eng::u32 x = static_cast<eng::u32>(bx) * 8u + bit;
						const bool dot = ((x & 7u) == 0u) && ((y & 7u) == 0u);
						const bool band = ((x + y) & 63u) < 32u;
						if (dot || band) v |= static_cast<eng::u8>(0x80u >> bit);
					}
					p[static_cast<eng::u32>(y) * kPatRowBytes + bx] = v;
				}
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;                                  // corkscrew (Y)
		scene_cfg.x_mode = eng::field::AxisMode::Finite;            // X lineal acotado
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;
		scene_cfg.parallax_plane = kParallaxPlane;                  // plano de fondo RoboCod
		scene_cfg.parallax_div = kParallaxDiv;
		scene_cfg.linear_display = true;                            // sin split -> raster colors
		scene_cfg.color_zones = kColorZones;
		scene_cfg.color_zone_count =
			static_cast<eng::u8>(sizeof(kColorZones) / sizeof(kColorZones[0]));

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		scene_cfg.map.wrap_x = 0;
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &fg_row;
		scene_cfg.bg_row_fn = &fg_row;
		scene_cfg.palette = kPalette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011202u);
			return;
		}
		scene.bg().set_camera(kViewportW / 4, 0);
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011203u);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011204u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x11200000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 200});
#ifdef K_DIAG_CYCLES
		const eng::u32 t0 = eng::debug::DebugPeripheral::cycle_counter();
#endif

		// Rebote diagonal sobre el area extra (X: 80 px; Y: 512-256).
		const eng::s32 camx = scene.bg().mapposx();
		const eng::s32 camy = scene.bg().mapposy();
		if (camx >= static_cast<eng::s32>(kMapCols * kTileW) - kViewportW) m_dx = -1;
		if (camx <= kGuardX) m_dx = 1;
		if (camy >= static_cast<eng::s32>(kMapRows * kTileH) - kViewportH) m_dy = -1;
		if (camy <= 0) m_dy = 1;
		const eng::s32 dx = (m_dx > 0) ? 2 : -2;
		const eng::s32 dy = (m_dy > 0) ? 1 : -1;
#ifndef K_DIAG_SKIP_SCROLL
		if (!scene.bg().update_scroll(plan, dx, dy)) {
			m_dx = -m_dx; m_dy = -m_dy;
		}
#endif
		// Soft DPF: copia la ventana del patrón de fondo al 5.º plano. El display
		// suma +camx (lee el bitmap en [camx, camx+viewport)), así que el patrón debe
		// situarse en `camx/div - camx = -camx*(div-1)/div` para que el fondo
		// scrollee a 1/div de la velocidad del FG. La parte no múltiplo de 16 la
		// resuelve el barrel shifter del Blitter -> posición de píxel exacta, sin
		// salto de columna cada 8/16 px. La copia deja una guarda (≤15 px) al
		// principio del bitmap que kGuardX mantiene fuera de la ventana.
		// El blit interleaved del FG pisa el 5.º plano; esta copia lo rehace DESPUÉS.
#ifndef K_DIAG_SKIP_BGCOPY
		if (m_bg_pattern.valid()) {
			const eng::s32 camx = scene.bg().mapposx();
			const eng::s32 src_px = field::parallax_pattern_offset_px(
				camx, kParallaxDiv, kPatPeriodPx);
			auto bg_job = scene.bg().make_bg_plane_copy_job(
				static_cast<const eng::u8*>(m_bg_pattern.data), kPatRowBytes,
				static_cast<eng::u16>(src_px), 0);
			plan.add_tile_block_copy(bg_job);
		}
#endif
		if (!backend.execute_frame_plan(plan)) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011210u); return;
		}
		if (!scene.compose()) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011211u); return;
		}
#ifdef K_DIAG_CYCLES
		const eng::u32 t2 = eng::debug::DebugPeripheral::cycle_counter();
		g_eng_run_status.detail = t2 - t0; // ciclos del update completo
		return;
#endif
		g_eng_run_status.detail = 0x11200000u |
			((static_cast<eng::u32>(scene.bg().mapposx()) & 0xffffu) << 8) |
			(static_cast<eng::u32>(scene.bg().mapposy()) & 0xffu);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
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
	engine.run_frames_polling(0xffff);
	return 0;
}
