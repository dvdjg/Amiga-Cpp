// Demo 207: capas vivas y animación de tiles sobre Beginning Fields.
//
// El bake base excluye las celdas animadas (Flowers/Water) y los objetos animados
// (Campfire). `cook-map.mjs` emite `animations.h` + banco planar + máscaras 1bpp;
// el runtime compone esas celdas y parchea solo los grupos cuyo frame cambia.
// La cámara queda fija para validar las tres animaciones sin confundirlas con el
// movimiento del scroll.
//
//   bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --debug --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/debug/prof.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/version.hpp>
#include <eng/field/tile_animation.hpp>
#include <eng/field/xlimited_scene.hpp>
#include <eng/map/asset.hpp>
#include <eng/platform/amiga/backend.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

#include "../../../../../../out/assets/tiled/beginning-fields-live-flowers/selected/map_bundle.h"
#include "../../../../../../out/assets/tiled/beginning-fields-live-flowers/selected/animations.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile eng::debug::RunStatus g_eng_run_status {
	eng::debug::run_status_magic,
	eng::debug::run_status_version,
	static_cast<eng::u16>(eng::debug::RunState::Cold),
	0,
	0,
};
#if defined(DEMO_BLIT_STATS)
__attribute__((used)) volatile eng::debug::FrameTelemetry g_eng_frame_telemetry { 0xffffffffu, 0u, 0u, 0u, 0u, { 0u, 0u, 0u } };
#endif
}

// Banco base interleaved (sin celdas animadas).
__asm__(".section tiles.MEMF_CHIP, \"aw\"\n"
	".globl g_tiled_bank\ng_tiled_bank:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields-live-flowers/selected/tilebank.xlimited.bin\"\n"
	".globl g_tiled_bank_size\ng_tiled_bank_size:\n"
	".long . - g_tiled_bank");
extern "C" const unsigned char g_tiled_bank[];
extern "C" const unsigned int g_tiled_bank_size;

// Frames planares 16x16 (6 planos) y máscaras 1bpp.
__asm__(".section anim_sheet.MEMF_CHIP, \"aw\"\n"
	".globl g_anim_sheet\ng_anim_sheet:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields-live-flowers/selected/anim_sheet.bin\"\n"
	".globl g_anim_sheet_size\ng_anim_sheet_size:\n"
	".long . - g_anim_sheet");
extern "C" const unsigned char g_anim_sheet[];
extern "C" const unsigned int g_anim_sheet_size;

__asm__(".section anim_masks.MEMF_CHIP, \"aw\"\n"
	".globl g_anim_masks\ng_anim_masks:\n"
	".align 2\n"
	".incbin \"out/assets/tiled/beginning-fields-live-flowers/selected/anim_masks.bin\"\n"
	".globl g_anim_masks_size\ng_anim_masks_size:\n"
	".long . - g_anim_masks");
extern "C" const unsigned char g_anim_masks[];
extern "C" const unsigned int g_anim_masks_size;

namespace {
namespace playfield = eng::playfield;

#if defined(DEMO_PROF)
#define DEMO_PROF_INIT(n) ENG_PROF_INIT(n)
#define DEMO_PROF_FRAME() ENG_PROF_FRAME()
#define DEMO_PROF_BEGIN(s) ENG_PROF_BEGIN(s)
#define DEMO_PROF_END(s) ENG_PROF_END(s)
#else
#define DEMO_PROF_INIT(n) do { (void)sizeof(n); } while (0)
#define DEMO_PROF_FRAME() do {} while (0)
#define DEMO_PROF_BEGIN(s) do { (void)sizeof(s); } while (0)
#define DEMO_PROF_END(s) do { (void)sizeof(s); } while (0)
#endif

#if defined(DEMO_BLIT_STATS)
inline void stat_name(eng::u8 slot, const char* name) {
	eng::debug::DebugPeripheral::counter_name(
		slot, static_cast<eng::u32>(reinterpret_cast<eng::usize>(name)));
}
inline void stat_value(eng::u8 slot, eng::u32 value) {
	eng::debug::DebugPeripheral::counter_value(slot, value);
}
#define DEMO_STAT_NAME(slot, name) stat_name(slot, name)
#define DEMO_STAT_VALUE(slot, value) stat_value(slot, value)
#else
#define DEMO_STAT_NAME(slot, name) do { (void)sizeof(slot); } while (0)
#define DEMO_STAT_VALUE(slot, value) do { (void)sizeof(value); } while (0)
#endif

const playfield::TileLayerBinding kLiveLayers[] = {
	{0u, playfield::TileLayerMode::AnimatedOverlay, false,
	 static_cast<eng::u8>(playfield::TileAnimationPriority::Background)},
	{1u, playfield::TileLayerMode::AnimatedOverlay, false,
	 static_cast<eng::u8>(playfield::TileAnimationPriority::Decor)},
	{2u, playfield::TileLayerMode::AnimatedOverlay, false,
	 static_cast<eng::u8>(playfield::TileAnimationPriority::Foreground)},
};


constexpr eng::u16 kTile = 16u;
constexpr eng::u16 kViewportW = 320u;
constexpr eng::u16 kViewportH = 256u; // total: 208 de campo + 48 de HUD negro
constexpr eng::u16 kFieldH = kViewportH - 48u;
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
	AnimationDraw = 0x00020716u,
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

	playfield::TileLayerStack layer_stack {};
	playfield::XlimitedScene<kScrollConsts, eng::map::LayerView> scene {};
	playfield::XlimitedSceneConfigT<eng::map::LayerView> scene_cfg {};
	eng::graphics::FramePlan plan {};
	eng::debug::HostHud host_hud {};
	bool ready = false;

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
		DEMO_PROF_INIT(8u);
		DEMO_STAT_NAME(0u, "jobs");
		DEMO_STAT_NAME(1u, "words");
		DEMO_STAT_NAME(2u, "masked");
		DEMO_STAT_NAME(3u, "copy");
		DEMO_STAT_NAME(4u, "groups");
		DEMO_STAT_NAME(5u, "tick");
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
		layer_stack.configure(eng::Span<const playfield::TileLayerBinding> {kLiveLayers, 3u});

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
		// Y=423 deja en pantalla agua, flores y la fogata a la vez.
		if (!scene.pre_scroll(backend, plan, 0, 300)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Scroll);
			return;
		}
		if (!scene.draw_animation_stack(backend, plan, layer_stack)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::AnimationDraw);
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

	static constexpr bool kBudgetedAnimations = false; // variante completa; true = política por defecto

	void apply_animation_budget() {
		if (!kBudgetedAnimations) {
			animator.set_layer_animation_enabled(0u, true);
			animator.set_layer_animation_enabled(1u, true);
			return;
		}
		const eng::u32 fps10 = host_hud.fps10();
		if (fps10 == 0u) return;
		if (fps10 < 480u) animator.set_layer_animation_enabled(0u, false);
		if (fps10 < 440u) animator.set_layer_animation_enabled(1u, false);
	}

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!ready) return;
		DEMO_PROF_FRAME();
		DEMO_PROF_BEGIN(0u);
		apply_animation_budget();
		animator.set_tick(context.frame.frame_index);
		plan.clear();
		plan.set_blit_budget_limits({16384, 65536, 120, 512});
		// Cámara fija: el update no desplaza el anillo; sigue siendo el punto de
		// sincronización de la escena y deja el parche de animación como única
		// fuente de cambios visuales.
		if (!scene.update(plan, 0, 0, context.frame.frame_index)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Scroll);
			return;
		}
		if (plan.blit_job_count() != 0u && !backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		DEMO_PROF_END(0u);
		DEMO_PROF_BEGIN(1u);
		plan.clear();
		plan.set_blit_budget_limits({16384, 65536, 120, 512});
		if (!scene.patch_animations(plan, static_cast<eng::u8>(1u << 2u))) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::AnimationDraw);
			return;
		}
		DEMO_PROF_END(1u);
#if defined(DEMO_BLIT_STATS)
		plan.finalize();
		const eng::graphics::BlitBudget& budget = plan.blit_budget();
		DEMO_STAT_VALUE(0u, budget.jobs);
		DEMO_STAT_VALUE(1u, budget.words);
		DEMO_STAT_VALUE(2u, budget.masked_jobs);
		DEMO_STAT_VALUE(3u, budget.copy_jobs);
		DEMO_STAT_VALUE(4u, animator.group_count());
		DEMO_STAT_VALUE(5u, animator.tick());
		g_eng_frame_telemetry.frame = context.frame.frame_index;
		g_eng_frame_telemetry.blit_jobs = budget.jobs;
		g_eng_frame_telemetry.blit_words = static_cast<eng::u16>(budget.words > 0xffffu ? 0xffffu : budget.words);
#endif
		DEMO_PROF_BEGIN(2u);
		if (plan.blit_job_count() != 0u && !backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		DEMO_PROF_END(2u);
		DEMO_PROF_BEGIN(3u);
		if (!scene.compose()) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::ComposeFrame);
			return;
		}
		DEMO_PROF_END(3u);
		DEMO_PROF_BEGIN(4u);
		eng::util::TextLine<96> stats;
		stats << "X=" << scene.bg().mapposx() << " Y=" << scene.bg().mapposy()
		      << " A=" << animator.frame_of(0u) << " G=" << animator.group_count();
#if defined(DEMO_BLIT_STATS)
		stats << " J=" << budget.jobs << " W=" << budget.words;
#endif
		host_hud.draw(backend.debug(), "207 TILED LIVE", kVersion, context.frame.frame_index,
			      eng::debug::prof_clock(), stats.c_str());
		DEMO_PROF_END(4u);
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
