// Demo 207: capas vivas y animación de tiles sobre Beginning Fields.
//
// El bake base excluye las celdas animadas (Flowers/Water) y los objetos animados
// (Campfire). `cook-map.mjs` emite `animations.h` + banco planar + máscaras 1bpp;
// el runtime compone esas celdas y parchea solo los grupos cuyo frame cambia.
// La cámara recorre el mapa en una visita con paradas de 3 s para observar las
// tres animaciones; durante el desplazamiento no se parchean las capas vivas.
//
//   bash tools/build/build-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation --debug --clean
//   bash tools/run/run-demo.sh demos/techniques/amiga/playfield/207_tiled_live_layers_animation

#include <eng/api/api.hpp>
#include <eng/core/util/text_line.hpp>
#include <eng/debug/host_hud.hpp>
#include <eng/debug/metrics.hpp>
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
#include "../../../../../../out/assets/tiled/beginning-fields-live-flowers/selected/tiled_map.h"
#include "../../../../../../out/assets/tiled/beginning-fields-live-flowers/selected/animations.h"

struct ExecBase* SysBase = nullptr;

#if defined(DEMO_DEBUG_ONE_CELL)
struct DebugAnimBlit {
	eng::u32 magic;
	eng::u32 dest;
	eng::u32 source;
	eng::u32 mask;
	eng::u16 words_per_row;
	eng::u16 height;
	eng::u32 target_base;
	eng::u32 plane_stride;
	eng::u16 row_bytes;
	eng::u8 planes;
	eng::u32 checksum_before;
	eng::u32 checksum_after;
	eng::u16 cell;
	eng::s16 screen_x;
	eng::s16 ring_y;
	eng::s16 camx;
	eng::s16 camy;
	eng::s16 display_offset;
	eng::u8 kind;
	eng::u8 minterm;
};
extern "C" __attribute__((used)) volatile DebugAnimBlit g_debug_anim_blit {};
#endif

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
constexpr eng::u16 kViewportH = 256u; // total: 208 de campo + 48 de HUD
constexpr eng::u16 kFieldH = kViewportH - 48u;
constexpr eng::u16 kMapW = TILED_MAP_W * kTile;
constexpr eng::u16 kMapH = TILED_MAP_H * kTile;
constexpr eng::u16 kTourPauseSeconds = 3u;
constexpr eng::s16 kTourStep = 2; // píxeles por eje y frame
constexpr eng::u8 kMaxTourStops = 8u;
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
	eng::playfield::TilePatchStats m_patch_stats {};
	struct CameraStop {
		eng::s16 x = 0;
		eng::s16 y = 0;
	};
	CameraStop m_stops[kMaxTourStops] {};
	eng::u8 m_stop_count = 0u;
	eng::u8 m_stop_index = 0u;
	eng::u16 m_pause_frames = 0u;
	eng::s32 m_camera_x = 0;
	eng::s32 m_camera_y = 0;
	bool ready = false;

	static constexpr eng::s16 wrap_camera(eng::s32 value, eng::s32 span) {
		value %= span;
		if (value < 0) value += span;
		return static_cast<eng::s16>(value);
	}

	/// Cámara centrada en la primera celda de una capa viva. Sirve para llevar
	/// la visita a una zona real de flores, agua o fogata sin conocer sus
	/// coordenadas en el código: las celdas vienen del cook-plan. Centrar el
	/// bounding box podía caer en terreno sin overlay (el agua se reparte en
	/// dos clusters separados).
	bool camera_for_layer(eng::u8 layer, eng::s16& x, eng::s16& y) const {
		eng::s32 min_x = 32767, min_y = 32767, max_x = -1, max_y = -1;
		for (eng::u16 i = 0u; i < animator.cell_count(); ++i) {
			const playfield::TileAnimationCellView& cell = animator.cell(i);
			if (cell.layer != layer) continue;
			if (min_x > cell.x) min_x = cell.x;
			if (min_y > cell.y) min_y = cell.y;
			if (max_x < cell.x) max_x = cell.x;
			if (max_y < cell.y) max_y = cell.y;
		}
		if (max_x < min_x) return false;
		const eng::s32 center_x = ((min_x + max_x + 1) * static_cast<eng::s32>(kTile)) / 2;
		const eng::s32 center_y = ((min_y + max_y + 1) * static_cast<eng::s32>(kTile)) / 2;
		x = wrap_camera(center_x - static_cast<eng::s32>(kViewportW) / 2, kMapW);
		y = wrap_camera(center_y - static_cast<eng::s32>(kFieldH) / 2, kMapH);
		return true;
	}

	eng::u16 pause_frames() const {
		// La pausa se mide en frames de la cadencia observada; si el HUD aún no
		// tiene una ventana válida, usar 34 fps (la cadencia típica de la visita).
		const eng::u32 fps10 = host_hud.fps10();
		const eng::u32 fps = fps10 >= 250u ? fps10 / 10u : 34u;
		return static_cast<eng::u16>(fps * kTourPauseSeconds);
	}

	void add_stop(eng::s16 x, eng::s16 y) {
		if (m_stop_count >= kMaxTourStops) return;
		for (eng::u8 i = 0u; i < m_stop_count; ++i) {
			if (m_stops[i].x == x && m_stops[i].y == y) return;
		}
		m_stops[m_stop_count++] = CameraStop {x, y};
	}

	/// Paradas: fogata (inicio), agua, flores, esquinas y centro del mapa. La
	/// cámara recorre todo el lienzo en una vuelta y vuelve a la fogata.
	void build_tour() {
		m_stop_count = 0u;
		eng::s16 x = 0, y = 0;
#if defined(DEMO_DEBUG_ONE_CELL)
		add_stop(0, 0); // diagnóstico: celda Flowers (4,1) en pantalla (64,16)
#endif
		if (camera_for_layer(2u, x, y)) add_stop(x, y); // fogata / Object Layer 1
		if (camera_for_layer(1u, x, y)) add_stop(x, y); // agua
		if (camera_for_layer(0u, x, y)) add_stop(x, y); // flores
		add_stop(0, 0);
		add_stop(static_cast<eng::s16>(kMapW - kViewportW), 0);
		add_stop(static_cast<eng::s16>(kMapW - kViewportW), static_cast<eng::s16>(kMapH - kFieldH));
		add_stop(0, static_cast<eng::s16>(kMapH - kFieldH));
		add_stop(static_cast<eng::s16>(kMapW / 2u), static_cast<eng::s16>(kMapH / 2u));
		if (m_stop_count == 0u) add_stop(0, 0);
		m_stop_index = m_stop_count > 1u ? 1u : 0u;
		m_camera_x = m_stops[0].x;
		m_camera_y = m_stops[0].y;
		m_pause_frames = pause_frames();
	}

	/// Avance de la vuelta: pausa 3 s en cada parada y después mueve la cámara
	/// hacia la siguiente a paso fijo. Devuelve el delta que consume `scene.update`.
	void update_tour(eng::s16& dx, eng::s16& dy) {
		dx = 0;
		dy = 0;
		if (m_pause_frames != 0u) {
			--m_pause_frames;
			return;
		}
		const CameraStop& target = m_stops[m_stop_index];
		const eng::s32 tx = target.x - m_camera_x;
		const eng::s32 ty = target.y - m_camera_y;
		dx = static_cast<eng::s16>(tx > kTourStep ? kTourStep : tx < -kTourStep ? -kTourStep : tx);
		dy = static_cast<eng::s16>(ty > kTourStep ? kTourStep : ty < -kTourStep ? -kTourStep : ty);
		m_camera_x += dx;
		m_camera_y += dy;
		if (m_camera_x == target.x && m_camera_y == target.y) {
			m_pause_frames = pause_frames();
			m_stop_index = static_cast<eng::u8>((m_stop_index + 1u) % m_stop_count);
		}
	}

	void init(eng::amiga::AmigaBackend& backend, eng::GameContext&) {
#if defined(DEMO_DEBUG_ONE_CELL)
		g_debug_anim_blit.magic = 0x414E494Du;
#endif
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
#if defined(DEMO_DEBUG_ONE_CELL)
		animator.set_test_cell(5u); // única celda Flowers del diagnóstico
#endif
		build_tour();
#if defined(DEMO_DEBUG_ONE_CELL)
		m_stop_index = 0u;
		m_camera_x = m_stops[0].x;
		m_camera_y = m_stops[0].y;
		m_pause_frames = 0xffffu; // congelar cámara para la celda única
#endif
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
		// La visita arranca centrada en la fogata; las capas vivas siguen
		// visibles mientras la cámara recorre el mapa.
		if (!scene.pre_scroll(backend, plan, m_stops[0].x, m_stops[0].y)) {
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
		eng::debug::metrics.begin_frame(context.frame.frame_index);
		DEMO_PROF_BEGIN(0u);
		apply_animation_budget();
		animator.set_tick(context.frame.frame_index);
		plan.clear();
		plan.set_blit_budget_limits({16384, 65536, 120, 512});
		eng::s16 tour_dx = 0, tour_dy = 0;
		update_tour(tour_dx, tour_dy);
		const bool tour_paused = m_pause_frames != 0u;
		if (!scene.update(plan, tour_dx, tour_dy, context.frame.frame_index)) {
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
		if (tour_paused) {
			eng::playfield::TilePatchStats* patch_stats_out = nullptr;
#if defined(DEMO_BLIT_STATS) || defined(ENG_METRICS)
			m_patch_stats = {};
			patch_stats_out = &m_patch_stats;
#endif
			if (!scene.patch_animations(plan, 0xffu, patch_stats_out)) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::AnimationDraw);
				return;
			}
			if constexpr (eng::debug::metrics_enabled) {
				eng::debug::metrics.set(eng::debug::Metric::TiledGroupsScanned, m_patch_stats.groups_scanned);
				eng::debug::metrics.set(eng::debug::Metric::TiledGroupsPatched, m_patch_stats.groups_patched);
				eng::debug::metrics.set(eng::debug::Metric::TiledGroupsDeferred, m_patch_stats.groups_deferred);
				eng::debug::metrics.set(eng::debug::Metric::TiledCellsPatched, m_patch_stats.cells_patched);
				eng::debug::metrics.set(eng::debug::Metric::TiledCellsCulled, m_patch_stats.cells_culled);
				eng::debug::metrics.set(eng::debug::Metric::TiledBaseRestores, m_patch_stats.base_restores);
				if (m_patch_stats.groups_deferred != 0u) {
					eng::debug::metrics.event(
						eng::debug::Metric::TiledGroupsDeferred, m_patch_stats.groups_deferred);
				}
			}
		}
#if defined(DEMO_DEBUG_ONE_CELL)
		if (plan.blit_job_count() != 0u) {
			const eng::graphics::BlitJob& job =
				plan.blit_job(static_cast<eng::u8>(plan.blit_job_count() - 1u));
			const eng::playfield::TileAnimationCellView& cell = animator.cell(5u);
			const eng::s32 wx = static_cast<eng::s32>(cell.x) * kTile;
			const eng::s32 wy = static_cast<eng::s32>(cell.y) * kTile;
			eng::s32 sx = (wx - scene.bg().mapposx()) % kMapW;
			if (sx < 0) sx += kMapW;
			eng::s32 sy = (wy - scene.bg().mapposy() - kTile) % kMapH;
			if (sy < 0) sy += kMapH;
			eng::s32 ry = (scene.bg().display_offset() + sy) %
				      scene.bg().display_height();
			g_debug_anim_blit.magic = 0x414E494Du; // "ANIM"
			g_debug_anim_blit.dest = reinterpret_cast<eng::u32>(job.destination.cwords());
			g_debug_anim_blit.source = reinterpret_cast<eng::u32>(job.source.cwords());
			g_debug_anim_blit.mask = reinterpret_cast<eng::u32>(job.mask.cwords());
			g_debug_anim_blit.words_per_row = job.words_per_row;
			g_debug_anim_blit.height = job.height;
			const auto ctx = scene.sprite_context();
			const auto& target = ctx.targets[0];
			g_debug_anim_blit.target_base =
				reinterpret_cast<eng::u32>(target.planes.data());
			g_debug_anim_blit.plane_stride = target.plane_pointer_step();
			g_debug_anim_blit.row_bytes = target.row_bytes;
			g_debug_anim_blit.planes = target.plane_count;
			eng::u32 sum = 0u;
			const eng::u16* words = job.destination.cwords();
			for (eng::u16 i = 0u; i < job.words_per_row * job.height; ++i) {
				sum += words[i];
			}
			g_debug_anim_blit.checksum_before = sum;
			g_debug_anim_blit.checksum_after = 0u;
			g_debug_anim_blit.cell = 5u;
			g_debug_anim_blit.screen_x = static_cast<eng::s16>(sx);
			g_debug_anim_blit.ring_y = static_cast<eng::s16>(ry);
			g_debug_anim_blit.camx = static_cast<eng::s16>(scene.bg().mapposx());
			g_debug_anim_blit.camy = static_cast<eng::s16>(scene.bg().mapposy());
			g_debug_anim_blit.display_offset =
				static_cast<eng::s16>(scene.bg().display_offset());
			g_debug_anim_blit.kind = static_cast<eng::u8>(job.kind);
			g_debug_anim_blit.minterm = static_cast<eng::u8>(job.minterm);
		}
#endif
		DEMO_PROF_END(1u);
		if constexpr (eng::debug::metrics_enabled) {
			plan.finalize();
			const eng::graphics::BlitBudget& b = plan.blit_budget();
			eng::debug::metrics.set(eng::debug::Metric::BlitJobs, b.jobs);
			eng::debug::metrics.set(eng::debug::Metric::BlitWords, b.words);
			eng::debug::metrics.set(eng::debug::Metric::BlitMaskedJobs, b.masked_jobs);
			eng::debug::metrics.set(eng::debug::Metric::BlitCopyJobs, b.copy_jobs);
		}
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
#if defined(DEMO_DEBUG_ONE_CELL)
		if (g_debug_anim_blit.magic == 0x414E494Du) {
			eng::u32 sum = 0u;
			const eng::u16* words =
				reinterpret_cast<const eng::u16*>(g_debug_anim_blit.dest);
			const eng::u16 count = static_cast<eng::u16>(
				g_debug_anim_blit.words_per_row * g_debug_anim_blit.height);
			for (eng::u16 i = 0u; i < count; ++i) sum += words[i];
			g_debug_anim_blit.checksum_after = sum;
		}
#endif
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
		stats << " J=" << budget.jobs << " B0=" << m_patch_stats.cells_patched_by_layer[0]
		      << " B1=" << m_patch_stats.cells_patched_by_layer[1]
		      << " B2=" << m_patch_stats.cells_patched_by_layer[2];
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
