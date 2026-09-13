// ============================================================================
// Demo 112 - RoboCod: DPF de 2 capas (FG plataformas + BG patron con parallax)
// ============================================================================
//
// El truco RoboCod clasico: un FG de videojuego (aqui plataformas) sobre un BG
// mas grande que scrollea MAS LENTO. Se implementa como **DPF 3+3** (dos campos
// XYLimited independientes, cada uno con su bitmap):
//   - FG (PF1, delante, colores 0..7): plataformas solidas (naranja), ~15% de la
//     pantalla; el resto transparente.
//   - BG (PF2, detras, colores 8..15): patron geometrico (bandas diagonales)
//     a la MITAD de velocidad (parallax) -> mismo movimiento, mas lento.
// La transparencia del color 0 de PF1 deja ver el BG por el 85% restante.
//
// Ambos campos scrollean en X e Y (corkscrew). Mundo FG 4096x512; mundo BG igual.

#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
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
constexpr eng::u8  kPlanes = 3;            // DPF 3+3 = 6 planos HW (8+8 colores)
constexpr eng::u16 kDisplayH = 288;        // corkscrew (256 + 2*16)

constexpr eng::u16 kMapCols = 64;          // 1024 px de ancho
constexpr eng::u16 kMapRows = 32;          // 512 px de alto
constexpr eng::u16 kTilesetCount = 32;

constexpr field::ScrollConsts kScrollConsts {
	/*tile_width=*/        kTileW,
	/*tile_height=*/       kTileH,
	/*display_height=*/    kDisplayH,
	/*display_planelines=*/static_cast<eng::u32>(kDisplayH) * kPlanes,
	/*planes=*/            kPlanes,
};

// --- Tiles FG (plataformas) --------------------------------------------------
// glyph 0 = transparente; glyph >=1 = bloque solido con borde superior claro.
eng::u16 fg_row(eng::u8 glyph, eng::u8, eng::u8 row, eng::u8 plane) {
	if (glyph == 0u) return 0u;
	const eng::u8 c = (row == 0u) ? 6u : 5u;   // borde claro / cuerpo (colores 1..7)
	return (c & (1u << plane)) != 0u ? 0xffffu : 0u;
}

// --- Tiles BG (patron geometrico) -------------------------------------------
// Bandas diagonales (color 6, con gradiente por raster) + un MOTIVO de puntos en
// las intersecciones cada 8 px (color 1): asi se ve que el fondo usa tiles.
eng::u16 bg_row(eng::u8, eng::u8, eng::u8 row, eng::u8 plane) {
	eng::u16 mask = 0u;
	for (eng::u8 x = 0; x < 16u; ++x) {
		const bool dot = ((x & 7u) == 0u) && ((row & 7u) == 0u);
		const bool band = ((x + row) & 15u) < 8u;
		eng::u8 c = 0u;
		if (dot) c = 1u;
		else if (band) c = 6u;
		if (c != 0u && (c & (1u << plane)) != 0u) mask |= static_cast<eng::u16>(0x8000u >> x);
	}
	return mask;
}

// Raster colors: el color del patron de fondo (COLOR14 = indice 6 de PF2) cambia
// por banda de raster a tonos PASTEL que siempre contrastan con el "off" (negro).
// Orden ascendente de linea. Requiere linear_display (sin split de Copper).
constexpr eng::copper::Register kBgReg = eng::copper::Register::COLOR14;
constexpr eng::field::XlimitedDualComposer::ColorZone kZones[] {
	{ 0x0034, kBgReg, 0xf9b }, // rosa
	{ 0x0054, kBgReg, 0xfd9 }, // melocoton
	{ 0x0074, kBgReg, 0xfb9 }, // amarillo limon
	{ 0x0094, kBgReg, 0xbf9 }, // menta
	{ 0x00b4, kBgReg, 0x9fb }, // aqua
	{ 0x00d4, kBgReg, 0x9bf }, // celeste
	{ 0x00f4, kBgReg, 0xd9f }, // lavanda
	{ 0x0114, kBgReg, 0xfbf }, // rosa palo
};

// Paleta DPF 16: FG (PF1) 0..7, BG (PF2) 8..15. PF2 color 1 = puntos (blanco);
// PF2 color 6 (reg 14) = bandas del patron (lo sobreescriben las zonas de raster).
constexpr eng::u16 kPalette[16] {
	0x000, 0x210, 0x320, 0x430, 0x540, 0xf80, 0xff0, 0xfff, // FG plataformas
	0x000, 0xfff, 0x025, 0x037, 0x049, 0x15b, 0x26d, 0x37f, // BG patron azul
};

eng::u16 g_fg_map[kMapCols * kMapRows] {};
eng::u16 g_bg_map[kMapCols * kMapRows] {};

struct DemoGame {
	field::XlimitedScene<kScrollConsts> scene {};
	field::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::s16 m_dx = 1, m_dy = 1;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({360u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011201u);
			return;
		}
		// FG: suelo + plataformas flotantes (poco % de la pantalla). BG: todo el
		// mismo tile de patron (bandas continuas).
		for (eng::u16 y = 0; y < kMapRows; ++y) {
			for (eng::u16 x = 0; x < kMapCols; ++x) {
				eng::u16 t = 0u;
				if (y >= static_cast<eng::u16>(kMapRows - 3u)) t = 1u;
				else if ((y == 8u || y == 16u || y == 22u) && ((x % 10u) < 4u)) t = 1u;
				else if ((y == 12u || y == 26u) && ((x % 14u) < 3u)) t = 2u;
				g_fg_map[static_cast<eng::u32>(y) * kMapCols + x] = t;
				g_bg_map[static_cast<eng::u32>(y) * kMapCols + x] = 0u;
			}
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTileW;
		scene_cfg.tile_height = kTileH;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0;
		scene_cfg.scroll_y = true;
		scene_cfg.scroll_mode = eng::field::ScrollMode::EightWay;
		scene_cfg.display_height = kDisplayH;
		scene_cfg.max_step = 4;
		scene_cfg.dual_linear_field = 0;   // corkscrew dual clasico (Y compartida)
		scene_cfg.linear_display = true;   // sin split de Copper -> raster colors OK

		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(g_fg_map, kMapCols * kMapRows);
		scene_cfg.map.width = kMapCols;
		scene_cfg.map.height = kMapRows;
		scene_cfg.map.wrap_x = kMapCols;
		scene_cfg.map.wrap_y = 0;
		scene_cfg.map.edge_tile = 0;
		scene_cfg.map2.cells = eng::Span<const eng::u16>::from_raw(g_bg_map, kMapCols * kMapRows);
		scene_cfg.map2.width = kMapCols;
		scene_cfg.map2.height = kMapRows;
		scene_cfg.map2.wrap_x = kMapCols;
		scene_cfg.map2.wrap_y = 0;
		scene_cfg.map2.edge_tile = 0;

		scene_cfg.tileset_count = kTilesetCount;
		scene_cfg.fg_row_fn = &fg_row;
		scene_cfg.bg_row_fn = &bg_row;
		scene_cfg.dpf.enabled = true;
		scene_cfg.dpf.color_zones = kZones;                    // raster colors del fondo
		scene_cfg.dpf.color_zone_count = static_cast<eng::u8>(sizeof(kZones) / sizeof(kZones[0]));
		scene_cfg.palette = kPalette;

		if (!scene.begin(backend.memory(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00011202u);
			return;
		}
		scene.bg().set_camera(0, 0);
		scene.fg().set_camera(0, 0);
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

		// Rebote diagonal sobre el area extra (X: 1024-320, Y: 512-256).
		const eng::s32 camx = scene.bg().mapposx();
		const eng::s32 camy = scene.bg().mapposy();
		if (camx >= static_cast<eng::s32>(kMapCols * kTileW) - kViewportW) m_dx = -1;
		if (camx <= 0) m_dx = 1;
		if (camy >= static_cast<eng::s32>(kMapRows * kTileH) - kViewportH) m_dy = -1;
		if (camy <= 0) m_dy = 1;
		// FG a velocidad completa; BG a la MITAD (parallax RoboCod).
		const eng::s32 dx = (m_dx > 0) ? 2 : -2;
		const eng::s32 dy = (m_dy > 0) ? 1 : -1;
		if (!scene.bg().update_scroll(plan, dx, dy)) { m_dx = -m_dx; m_dy = -m_dy; }
		scene.fg().update_scroll(plan, dx / 2, dy / 2);

		if (!backend.execute_frame_plan(plan)) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011210u); return;
		}
		if (!scene.compose()) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00011211u); return;
		}
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
