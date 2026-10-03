// Demo 205 — el MISMO pueblito que la 204, pero por el camino **XYLimited** (corcóscru) con
// viewport **320×208** y el **mínimo framebuffer**: el anillo es `(320+guarda) × (208+2·16)` (~30 KB
// a 3 planos) frente a los ~158 KB de la 204. X e Y de verdad (8 direcciones), no X de tiras + Y de
// puntero.
//
//   bash ./tools/build/build-demo.sh demos/techniques/amiga/playfield/205_xy_limited_scroll --release
//   bash ./tools/run/run-demo.sh demos/techniques/amiga/playfield/205_xy_limited_scroll

#include <eng/api/api.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/debug/prof.hpp>
// La ruta de scroll llega por la fachada (`eng/api/scroll.hpp`); `xlimited_scene` es el MOTOR de la
// técnica del corcóscru que esta demo de `techniques/` demuestra directamente.
#include <eng/field/xlimited_scene.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic, eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold), 0, 0,
};
}

// Atlas "Beginning Fields" a 8 colores (paleta + mapa 40x40 de ids de tile).
#include "../../../../../../out/assets/beginning-fields/8c/tilebank_8c_t16_mediancut_none_640x640.h"

// Banco X-Limited interleaved YA listo (3 planos, 320 px) producido por amiga-tiles; el corcóscru
// lo usa como `blocks_prebuilt` (se alía, no se copia). Mismo banco que la 204.
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_xlim\ng_xlim:\n"
	".align 2\n"
	".incbin \"out/assets/beginning-fields/8c/tilebank_xlimited_8c_t16_mediancut_none.bin\"\n"
	".globl g_xlim_size\ng_xlim_size:\n"
	".long . - g_xlim");
extern "C" const unsigned char g_xlim[];
extern "C" const unsigned int g_xlim_size;

namespace {

namespace playfield = eng::playfield;

constexpr eng::u16 kWidth = 320u;
constexpr eng::u16 kHeight = 208u; // viewport del corcóscru (split canónico, caben 208 en VPOS)
constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kMapSide = 40u;
constexpr eng::u8 kPlanes = 3u;
constexpr eng::u16 kTilesetCount = 1180u;
// Anillo vertical del corcóscru: 288 para viewport 208 (como la 202); con 240 el fondo leía filas
// equivocadas del anillo (mapa roto en la mitad inferior).
constexpr eng::u16 kDisplayH = 320u;

constexpr playfield::ScrollConsts kScroll {
	kTile, kTile, kDisplayH, static_cast<eng::u32>(kDisplayH) * kPlanes, kPlanes};

using MapT = playfield::TileLayerMap;
using Profile = playfield::ScrollProgressive;

// Secciones de perfilado (tools/debug/profile.mjs): planificar+blitear vs recomponer el fondo.
enum : eng::u8 { kProfUpdate = 0, kProfCompose, kProfCount };

eng::u16 g_palette[8] {};

/// La Y se mantiene en **`[0, kYMax]`**: el corcóscru single-field solo reconstruye bien la ventana
/// mientras NO envuelve el anillo (sin split: `y + tile <= display_height - viewport_h`).
/// `kYMax = display_height - viewport_h - tile = 288 - 208 - 16 = 64`.
constexpr eng::u16 kYMax = 112u; // display_height - viewport_h (recorrido del anillo)
/// Ruta de scroll **continua** del engine (mismos fases que la 204), por velocidad y <= 1 px/frame.
using Route = eng::playfield::ScrollRoute<kYMax>;

struct DemoGame {
	playfield::XlimitedScene<kScroll, MapT, Profile> scene {};
	playfield::XlimitedSceneConfigT<MapT> config {};
	eng::graphics::FramePlan plan {};
	Route route {};
	eng::s32 prev_x = 1;
	eng::s32 prev_y = route.y;
	bool ready = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({160u * 1024u, 16u * 1024u, 0u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020501u);
			return;
		}
		for (eng::u32 i = 0u; i < 8u; ++i) {
			const eng::u8 r = static_cast<eng::u8>(::kPalette[i * 3u]);
			const eng::u8 g = static_cast<eng::u8>(::kPalette[i * 3u + 1u]);
			const eng::u8 b = static_cast<eng::u8>(::kPalette[i * 3u + 2u]);
			g_palette[i] = static_cast<eng::u16>(((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
		}

		config.viewport_w = kWidth;
		config.viewport_h = kHeight;
		config.tile_width = kTile;
		config.tile_height = kTile;
		config.planes = kPlanes;
		config.fetch_mode = 0u;
		config.y_mode = playfield::AxisPolicy::Ring; // corcóscru (anillo vertical + split)
		config.display_height = kDisplayH;
		config.linear_display = false;
		config.direction = playfield::DirectionPolicy::Bidirectional;
		config.max_step = 1u;
		config.visible_tile_bias_x = 1u;
		config.visible_tile_bias_y = 1u;
		// Mapa toroidal del atlas (40×40 de ids de tile).
		config.map.cells = eng::Span<const eng::u16> {&kTileIndexedMap[0][0], kMapSide * kMapSide};
		config.map.width = kMapSide;
		config.map.height = kMapSide;
		config.map.wrap_x = kMapSide;
		config.map.wrap_y = kMapSide;
		config.map.edge_tile = 0u;
		config.map.empty_tile = 0xffffu;
		config.tileset_count = kTilesetCount;
		// El banco X-Limited del atlas se ALÍA (no se copia): mínimo framebuffer/banco.
		config.blocks_prebuilt = g_xlim;
		config.blocks_prebuilt_size = g_xlim_size;
		config.palette = eng::PaletteWords {g_palette, 8u};
		if (!scene.begin(backend.memory_manager(), config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020502u);
			return;
		}
		scene.bg().set_camera(1, 96);
		if (!scene.fill(backend, plan) || !scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00020503u);
			return;
		}
		scene.takeover(backend);
		ready = true;
		ENG_PROF_INIT(kProfCount);
		eng::debug::mark_ready(g_eng_run_status, 0x20500000u);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		route.advance(context.frame.frame_index);
		const eng::s32 dx = route.x - prev_x;
		const eng::s32 dy = route.y - prev_y;
		prev_x = route.x;
		prev_y = route.y;
		plan.clear();
		plan.set_blit_budget_limits({65536, 262144, 64, 8192});
		ENG_PROF_BEGIN(kProfUpdate);
		const bool ok_update =
			scene.update(plan, dx, dy, context.frame.frame_index) && backend.execute_frame_plan(plan);
		ENG_PROF_END(kProfUpdate);
		ENG_PROF_BEGIN(kProfCompose);
		const bool ok_compose = ok_update && scene.compose();
		ENG_PROF_END(kProfCompose);
		if (!ok_update || !ok_compose) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00020504u);
			return;
		}
		g_eng_run_status.detail = 0x20500000u | ((static_cast<eng::u32>(route.x) & 0xffu) << 12) |
					  (static_cast<eng::u32>(route.y) & 0xfffu);
	}

	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		ENG_PROF_FRAME();
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::AmigaBackend backend {};
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
