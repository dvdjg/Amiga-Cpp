// Demo 120 — PROC WORLD SCROLL: el mundo de biomas de `eng::gfx::proc` con SCROLL.
// ----------------------------------------------------------------------------
// Tutorial: el mundo generado EN COMPILACIÓN (`constexpr`, ver la demo 119) se publica
// como **mapa de scroll X-Limited**: el `PatternLayout` de 40×28 celdas de 16 px se
// envuelve como `TileLayerMap` (toroidal) y `XlimitedScene` lo recorre con el corkscrew
// OCS — solo se blittea la columna que entra al cruzar cada límite de 16 px; el resto de
// la pantalla no se toca. La cámara avanza 1 px/frame en X de forma continua.
//
// Piezas del sistema:
//   - **Mundo**: `generate_biome_world` constexpr (91 piezas de 16×16, paleta de 32) —
//     `tools/gen_assets.cpp` de la 119 emite los MISMOS datos como assets offline.
//   - **Banco de tiles**: `indexed_tiles` (1 B/píxel, stride 256) → la escena lo convierte
//     en el banco interleaved X-Limited al arrancar (5 planos estándar, sin EHB).
//   - **Scroll**: `XlimitedScene` con `ScrollConsts` (divisores en compilación, sin
//     `__udivsi3` en el camino caliente) y conductor de presupuesto de Blitter.
//   - **HUD**: el overlay de depuración del host (`HostHud`) muestra nombre + número +
//     versión + frame + fps. La franja inferior de 48 px se conserva para mantener la
//     geometría del corkscrew, pero ya no contiene texto de depuración.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --debug
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/effects/120_proc_world_scroll --warp
//
// (El build tarda ~1,5 min de más: el compilador evalúa el atlas + el mundo 40×28.)
#include <eng/api/api.hpp>
#include <eng/core/math/minifloat.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/terrain_biome.hpp>
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

using eng::u8;
using eng::u16;
using eng::u32;
using MF = eng::math::MiniFloat16;
namespace proc = eng::gfx::proc;
namespace terr = eng::gfx::proc::terrain;
namespace playfield = eng::playfield;

// -----------------------------------------------------------------------------
// Mundo (generado EN COMPILACIÓN: mismas semillas que la 119 y la vía offline)
// -----------------------------------------------------------------------------
constexpr u32 kAtlasSeed = 0x7E5A1u;
constexpr u32 kMapSeed = 0xBEE5u;
constexpr u16 kTile = 16u;
constexpr u16 kMapW = 40u;
constexpr u16 kMapH = 28u;

[[nodiscard]] constexpr proc::TextureAtlas<kTile, kTile, MF> make_world_atlas() {
	proc::TextureAtlas<kTile, kTile, MF> a {};
	a.generate(kAtlasSeed);
	return a;
}
inline constexpr proc::TextureAtlas<kTile, kTile, MF> kWorldAtlas = make_world_atlas();

/// Mundo generado en compilación (layout + set); el banco de tiles se construye desde
/// aquí con `indexed_tiles` (1 B/píxel por tile: stride 256 = 16×16).
struct WorldData {
	terr::TerrainLayout<kMapW, kMapH> layout {};
	terr::BiomeTileSet<kTile, kTile, 400u> set {};
};
[[nodiscard]] constexpr WorldData make_world() {
	WorldData w {};
	(void)terr::generate_biome_world<kMapW, kMapH>(w.layout, w.set, kWorldAtlas, kMapSeed);
	return w;
}
inline constexpr WorldData kWorld = make_world();
static_assert(kWorld.set.count <= 400u, "el set del mundo grande cabe en el banco");

// Vista del tilebank INDEXADO: los `BitmapBuffer<16,16>` del set son contiguos (pixels de
// 256 B por tile) y viven en rodata — la escena los lee tal cual para construir el banco.
[[nodiscard]] constexpr eng::TileBankBytes indexed_tiles_view() {
	return eng::TileBankBytes {kWorld.set.tiles[0].pixels, u32(kWorld.set.count) * 256u};
}

// -----------------------------------------------------------------------------
// Geometría del scroll (constantes a priori: divisores en compilación)
// -----------------------------------------------------------------------------
constexpr u16 kViewportW = 320u;
constexpr u16 kViewportH = 256u;             // total en pantalla (campo + HUD)
constexpr u16 kHudH = 48u;                   // franja inferior (4 planos, paleta propia)
constexpr u16 kMainH = u16(kViewportH - kHudH); // campo visible: 208 (corkscrew holgado)
constexpr u8 kPlanes = 5u;                   // 32 colores estándar (los del mundo)
constexpr u16 kDisplayH = u16(kViewportH + 2u * kTile); // anillo: 288 (invariante 201)
constexpr playfield::ScrollConsts kScrollConsts {
	/*tile_width=*/kTile,
	/*tile_height=*/kTile,
	/*display_height=*/kDisplayH,
	/*display_planelines=*/u16(kDisplayH * kPlanes),
	/*planes=*/kPlanes,
};

/// Formato de la demo (major.minor; el build lo inyecta como -DENG_BUILD_NUMBER).
constexpr eng::debug::Version kVersion {1u, 0u};

/// Códigos de fallo de la demo 120 (byte alto = demo, bajo = etapa).
enum class Fail : eng::u32 {
	Memory = 0x00012001u,       ///< `configure_memory`
	SceneBegin = 0x00012002u,   ///< `scene.begin`
	Fill = 0x00012003u,         ///< `scene.fill`
	ComposeInit = 0x00012004u,  ///< `scene.compose` inicial
	Blitter = 0x00012010u,      ///< `execute_frame_plan`
	ComposeFrame = 0x00012011u, ///< `scene.compose` por frame
};

/// `detail` de `mark_ready`: identifica la demo 120 en el canal lateral.
constexpr eng::u32 kReadyDetail = 0x00000120u;

// Paleta de la franja reservada (sin texto): fondo negro. La franja mantiene la
// geometría del corkscrew; el HUD real vive en el overlay del host.
eng::u16 g_hud_palette[2] { 0x000u, 0x000u };
// Paleta del CAMPO (32 colores del atlas; se rellena en `init` desde el constexpr).
eng::u16 g_world_palette[32] {};

// -----------------------------------------------------------------------------
// Juego
// -----------------------------------------------------------------------------
struct ScrollWorldDemo {
	playfield::XlimitedScene<kScrollConsts> scene {};
	playfield::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	u32 m_frames = 0u;
	bool m_ready = false;
	eng::debug::HostHud m_host_hud {}; ///< HUD del host (nombre/número/versión/frame/fps)

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// Chip: anillo (208+32 filas × 5 planos) + banco de bloques (~16 KB) + copper.
		if (!backend.configure_memory({200u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud.height = kHudH;
		// La franja se reserva para conservar la geometría del corkscrew; no pinta texto.
		scene_cfg.hud.planes = 1u;
		scene_cfg.hud.palette = g_hud_palette;
		scene_cfg.tile_width = kTile;
		scene_cfg.tile_height = kTile;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0u;
		scene_cfg.y_mode = playfield::AxisPolicy::Ring;
		scene_cfg.direction = playfield::DirectionPolicy::Bidirectional;
		scene_cfg.linear_display = false; // main=208: el split cabe en el comparador de 8 bits
		scene_cfg.max_step = 2u;
		scene_cfg.map.cells = kWorld.layout.span();
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = kMapW; // toroidal en X: scroll continuo sin bordes
		scene_cfg.map.wrap_y = kMapH;
		scene_cfg.map.edge_tile = 0u;
		scene_cfg.map.empty_tile = 0xFFFFu;
		scene_cfg.visible_tile_bias_x = 1u; // offset (0,0) visible = map[0][0]
		scene_cfg.visible_tile_bias_y = 1u;
		scene_cfg.tileset_count = kWorld.set.count;
		scene_cfg.indexed_tiles = indexed_tiles_view();
		scene_cfg.indexed_stride = 256u; // bytes por tile (16×16, 1 B/píxel)
		for (u8 i = 0u; i < 32u; ++i) {
			g_world_palette[i] = kWorldAtlas.palette.color[i];
		}
		scene_cfg.palette = g_world_palette;

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::SceneBegin);
			return;
		}
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Fill);
			return;
		}
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::ComposeInit);
			return;
		}
		scene.takeover(backend);
		m_ready = true;
		eng::debug::mark_ready(g_eng_run_status, kReadyDetail);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!m_ready) {
			return;
		}
		// Ablaciones de medición (compiladas, temporales): saltan una etapa para cuantificar
		// su coste con `measure-fps` (bit0 compose, bit1 update/execute, bit2 install).
#if defined(K120_SKIP_COMPOSE)
		const u8 ab = 1u;
#elif defined(K120_SKIP_UPDATE)
		const u8 ab = 2u;
#elif defined(K120_SKIP_INSTALL)
		const u8 ab = 4u;
#else
		const u8 ab = 0u;
#endif
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
		if (
#if defined(K120_PROBE_ABLATE)
			(ab & 2u) == 0u
#else
			true
#endif
		) {
			// Scroll X continuo de 1 px/frame (Y fija): al cruzar cada límite de 16 px
			// la escena blittea solo la columna entrante.
			scene.update(plan, 1, 0, context.frame.frame_index);
			if (!backend.execute_frame_plan(plan)) {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
				return;
			}
		}
		if (
#if defined(K120_PROBE_ABLATE)
			(ab & 1u) == 0u
#else
			true
#endif
		) {
			if (!scene.compose()) {
				m_ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::ComposeFrame);
				return;
			}
		}
		++m_frames;
		m_host_hud.draw(backend.debug(), "120 PROC WORLD SCROLL", kVersion, m_frames,
				eng::debug::prof_clock());
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (m_ready) {
#if defined(K120_SKIP_INSTALL)
			const u8 ab = 4u;
#else
			const u8 ab = 0u;
#endif
			if ((ab & 4u) == 0u) {
				scene.install(backend);
			}
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
};

static_assert(eng::DisplayDriver<playfield::XlimitedScene<kScrollConsts>, eng::amiga::AmigaBackend>);

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::AmigaBackend backend {};
	ScrollWorldDemo game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffff);

	return 0;
}
