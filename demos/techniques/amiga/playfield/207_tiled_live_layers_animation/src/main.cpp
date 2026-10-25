// Demo 207: capas vivas y animación de tiles sobre Beginning Fields.
//
// El bake base excluye las celdas animadas (Water/Flowers) y los objetos animados
// (Campfire). `cook-map.mjs` emite `animations.h` + banco planar + máscaras 1bpp;
// el runtime compone esas celdas en la banda entrante y parchea solo los grupos
// cuyo frame cambia. El scroll es XY (corkscrew) para que la fogata sea visible.
//
//   bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --debug --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/tile_animation.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/map/asset.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

#include "../../../../../../out/assets/tiled/beginning-fields/selected/map_bundle.h"
#include "../../../../../../out/assets/tiled/beginning-fields/selected/animations.h"

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

// Banco base interleaved (sin celdas animadas).
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tiled_bank\ng_tiled_bank:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields/selected/tilebank.xlimited.bin\"\n"
	".globl g_tiled_bank_size\ng_tiled_bank_size:\n"
	".long . - g_tiled_bank");
extern "C" const unsigned char g_tiled_bank[];
extern "C" const unsigned int g_tiled_bank_size;

// Frames planares 16x16 (6 planos) y máscaras 1bpp.
__asm__(".section anim_sheet.MEMF_CHIP, \"aw\"\n"
	".globl g_anim_sheet\ng_anim_sheet:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields/selected/anim_sheet.bin\"\n"
	".globl g_anim_sheet_size\ng_anim_sheet_size:\n"
	".long . - g_anim_sheet");
extern "C" const unsigned char g_anim_sheet[];
extern "C" const unsigned int g_anim_sheet_size;

__asm__(".section anim_masks.MEMF_CHIP, \"aw\"\n"
	".globl g_anim_masks\ng_anim_masks:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields/selected/anim_masks.bin\"\n"
	".globl g_anim_masks_size\ng_anim_masks_size:\n"
	".long . - g_anim_masks");
extern "C" const unsigned char g_anim_masks[];
extern "C" const unsigned int g_anim_masks_size;

namespace {

namespace playfield = eng::playfield;

constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kViewportW = 320u;
constexpr eng::u16 kViewportH = 256u; // total: 208 de campo + 48 de HUD negro
constexpr eng::u8 kPlanes = TILED_MAP_PLANES;
constexpr eng::u16 kPaletteCount = (kPlanes == 6u) ? 32u : (1u << kPlanes);
constexpr eng::debug::Version kVersion {1u, 0u};
constexpr eng::u32 kReadyDetail = 0x20700000u;

// Corkscrew XY: anillo 288 (208 visibles + 2 filas de staging).
constexpr playfield::ScrollConsts kScrollConsts {
	kTile, kTile, 288u, 288u * kPlanes, kPlanes};

enum class Fail : eng::u32 {
	Memory = 0x00020701u,
	MapAsset = 0x00020705u,
	Animation = 0x00020706u,
	SceneBegin = 0x00020702u,
	Fill = 0x00020703u,
	ComposeInit = 0x00020704u,
	Scroll = 0x00020712u,
	Blitter = 0x00020710u,
	ComposeFrame = 0x00020711u,
};

eng::u16 g_palette[32] {};
eng::u16 g_hud_palette[16] {};

void build_palette(const eng::map::MapAssetView& map) {
	const eng::u16 count = map.palette_count() < kPaletteCount ? map.palette_count() : kPaletteCount;
	for (eng::u16 i = 0u; i < count; ++i) g_palette[i] = map.palette_word(i);
}

struct DemoGame {
	eng::map::MapAssetView m_map {eng::Span<const eng::u8> {kMapBundle, sizeof(kMapBundle)}};
	eng::map::LayerView m_layer {};
	playfield::TileAnimator animator {};
	playfield::XlimitedScene<kScrollConsts, eng::map::LayerView> scene {};
	playfield::XlimitedSceneConfigT<eng::map::LayerView> scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::debug::HostHud host_hud {};
	eng::s16 direction = 1;
	eng::u32 frame_of_day = 0u;
	bool ready = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({104u * 1024u, 16u * 1024u, 0u})) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Memory);
			return;
		}
		if (!m_map.valid() || m_map.planes() != kPlanes || !m_map.palette_count()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::MapAsset);
			return;
		}
		m_layer = eng::map::LayerView {m_map, 0u};
		if (!m_layer.has_data()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::MapAsset);
			return;
		}
		build_palette(m_map);

		animator.configure(
			eng::Span<const playfield::TileAnimationGroupView> {
				kTiledAnimGroups, TILED_ANIM_GROUP_COUNT},
			eng::Span<const playfield::TileAnimationLayerView> {
				kTiledAnimLayers, TILED_ANIM_LAYER_COUNT},
			eng::Span<const playfield::TileAnimationCellView> {
				kTiledAnimCells, TILED_ANIM_CELL_COUNT},
			g_anim_sheet, g_anim_masks,
			TILED_ANIM_FRAME_STRIDE, TILED_ANIM_MASK_STRIDE,
			TILED_ANIM_PLANES, TILED_ANIM_TILE);
		if (!animator.configured() || g_anim_sheet_size != TILED_ANIM_SHEET_BYTES ||
		    g_anim_masks_size != TILED_ANIM_MASK_BYTES) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Animation);
			return;
		}
		animator.set_tick(0u);

		scene_cfg.viewport_w = kViewportW;
		scene_cfg.viewport_h = kViewportH;
		scene_cfg.hud.height = 48u;
		scene_cfg.hud.planes = 4u;
		scene_cfg.hud.palette = g_hud_palette;
		scene_cfg.tile_width = kTile;
		scene_cfg.tile_height = kTile;
		scene_cfg.planes = m_map.planes();
		scene_cfg.fetch_mode = 0u;
		scene_cfg.y_mode = playfield::AxisPolicy::Ring;
		scene_cfg.direction = playfield::DirectionPolicy::Bidirectional;
		scene_cfg.display_height = 288u;
		scene_cfg.max_step = 1u;
		scene_cfg.map = m_layer;
		scene_cfg.tileset_count = m_map.tile_count();
		scene_cfg.blocks_prebuilt = g_tiled_bank;
		scene_cfg.blocks_prebuilt_size = g_tiled_bank_size;
		scene_cfg.palette = g_palette;
		scene_cfg.animator = &animator;

		if (!scene.begin(backend.memory_manager(), scene_cfg)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::SceneBegin);
			return;
		}
		if (!scene.fill(backend, plan)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Fill);
			return;
		}
		if (!scene.pre_scroll(backend, plan, 0, 300)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Scroll);
			return;
		}
		animator.clear_changes();
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
		if ((context.frame.frame_index % 600u) == 0u && context.frame.frame_index != 0u) {
			direction = static_cast<eng::s16>(-direction);
		}
		animator.set_tick(context.frame.frame_index);
		plan.clear();
		plan.set_blit_budget_limits({16384, 65536, 256, 512});
		const eng::s32 step = static_cast<eng::s32>(direction);
		const eng::s32 dx = 0; // recorrido Y-only: X+scroll simultáneo baja a 25 fps
		if (!scene.update(plan, dx, step, context.frame.frame_index)) {
			// Borde del toroide inverso: rebotar y reintentar en el mismo frame.
			direction = static_cast<eng::s16>(-direction);
			plan.clear();
			plan.set_blit_budget_limits({16384, 65536, 256, 512});
			if (!scene.update(plan, dx, direction, context.frame.frame_index)) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::Scroll);
				return;
			}
		}
		if (!scene.patch_animations(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Animation);
			return;
		}
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
		stats << "X=" << scene.bg().mapposx() << " Y=" << scene.bg().mapposy()
		      << " A=" << animator.frame_of(0u) << " G=" << animator.group_count();
		host_hud.draw(backend.debug(), "207 TILED LIVE", kVersion, context.frame.frame_index,
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
