// Demo 206: mapa de Tiled cocinado en host y renderizado con la familia XLimited.
//
// El Tiled project `Beginning Fields` se compone en `tools/tiled/compose-map.mjs`,
// se cocina a EHB con `tools/tiled/cook-map.mjs` y se consume aquí como banco
// interleaved prehecho + mapa de índices. La demo solo mueve la cámara: no parsea
// TMX/TSX/PNG, no cuantiza y no transforma índices en el Amiga.
//
// La selección de perfil/paleta se registra en
// `out/assets/tiled/beginning-fields/proposal.md` (16c/32c/EHB); este build usa
// `selected/` (EHB por la capa de sombras). Cambiar el perfil es volver a cocinar
// y recompilar; el código de la demo no cambia.
//
//   bash tools/build/build-demo.sh demos/techniques/amiga/playfield/206_tiled_beginning_fields_baked --debug --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/playfield/206_tiled_beginning_fields_baked

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

// Mapa, paleta y celdas cocinados (perfil seleccionado).
#include "../../../../../../out/assets/tiled/beginning-fields/selected/tiled_map.h"

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

// Banco de bloques interleaved ya cocido en host (sin copia en Chip).
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tiled_bank\ng_tiled_bank:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields/selected/tilebank.xlimited.bin\"\n"
	".globl g_tiled_bank_size\ng_tiled_bank_size:\n"
	".long . - g_tiled_bank");
extern "C" const unsigned char g_tiled_bank[];
extern "C" const unsigned int g_tiled_bank_size;

namespace {

namespace playfield = eng::playfield;

constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kViewportW = 320u;
constexpr eng::u16 kViewportH = 256u;
constexpr eng::u8 kPlanes = TILED_MAP_PLANES;
constexpr eng::u16 kMapW = TILED_MAP_W;
constexpr eng::u16 kMapH = TILED_MAP_H;
constexpr eng::u16 kTileCount = TILED_MAP_TILES;
constexpr eng::u16 kPaletteCount = (kPlanes == 6u) ? 32u : (1u << kPlanes);
constexpr eng::debug::Version kVersion {1u, 0u};
constexpr eng::u32 kReadyDetail = 0x20600000u;

// X-only: el anillo del corkscrew se dimensiona con el viewport; Y queda fijo.
constexpr playfield::ScrollConsts kScrollConsts {
	kTile, kTile, kViewportH, static_cast<eng::u32>(kViewportH) * kPlanes, kPlanes};

enum class Fail : eng::u32 {
	Memory = 0x00020601u,
	SceneBegin = 0x00020602u,
	Fill = 0x00020603u,
	ComposeInit = 0x00020604u,
	Blitter = 0x00020610u,
	ComposeFrame = 0x00020611u,
};

eng::u16 g_palette[32] {};

void build_palette() {
	// `kTileIndexedPalette` es RGB888 en orden de índice. En EHB solo se cargan
	// las 32 bases; el hardware deriva los half (COLOR32..63).
	for (eng::u16 i = 0u; i < kPaletteCount; ++i) {
		const eng::u8* c = &kTileIndexedPalette[static_cast<eng::u32>(i) * 3u];
		g_palette[i] = static_cast<eng::u16>(((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4));
	}
}

struct DemoGame {
	playfield::XlimitedScene<kScrollConsts> scene {};
	playfield::XlimitedSceneConfig scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::debug::HostHud host_hud {};
	eng::s16 direction = 1;
	eng::u32 frame_of_day = 0u;
	bool ready = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({176u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		build_palette();

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.tile_width = kTile;
		scene_cfg.tile_height = kTile;
		scene_cfg.planes = kPlanes;
		scene_cfg.fetch_mode = 0u;
		scene_cfg.y_mode = playfield::AxisPolicy::Off;
		scene_cfg.direction = playfield::DirectionPolicy::Bidirectional;
		scene_cfg.display_height = kViewportH;
		scene_cfg.max_step = 4u;
		scene_cfg.map.cells = eng::Span<const eng::u16>::from_raw(
			&kTileIndexedMap[0][0], static_cast<eng::usize>(kMapW) * kMapH);
		scene_cfg.map.width = kMapW;
		scene_cfg.map.height = kMapH;
		scene_cfg.map.wrap_x = kMapW; // mundo horizontal toroidal
		scene_cfg.map.wrap_y = 0u;
		scene_cfg.map.edge_tile = 0u;
		scene_cfg.map.empty_tile = 0xFFFFu;
		scene_cfg.tileset_count = kTileCount;
		scene_cfg.blocks_prebuilt = g_tiled_bank;
		scene_cfg.blocks_prebuilt_size = g_tiled_bank_size;
		scene_cfg.palette = g_palette;

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
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, kReadyDetail);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!ready) return;
		// Cambia de sentido cada 400 frames para que la secuencia de visión vea
		// ambos recorridos sin que la cámara salga del mundo toroidal.
		if ((context.frame.frame_index % 400u) == 0u && context.frame.frame_index != 0u) direction = static_cast<eng::s16>(-direction);
		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 6, 120});
		const eng::s32 dx = static_cast<eng::s32>(direction) * 2;
		scene.update(plan, dx, 0, context.frame.frame_index);
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::ComposeFrame);
			return;
		}
		++frame_of_day;
		eng::util::TextLine<64> stats;
		stats << "X=" << scene.bg().mapposx() << " T=" << kTileCount << " P=" << eng::u16(kPlanes);
		host_hud.draw(backend.debug(), "206 TILED BAKED", kVersion, context.frame.frame_index,
			      eng::debug::prof_clock(), stats.c_str());
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
	DemoGame game {};
	eng::Engine engine {backend, game};
	engine.run_frames_polling(0xffffu);
	return 0;
}
