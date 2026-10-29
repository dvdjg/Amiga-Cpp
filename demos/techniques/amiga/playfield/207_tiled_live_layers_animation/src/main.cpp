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
#ifndef DEMO_DEBUG_CELL
#if defined(DEMO_SEAM)
#define DEMO_DEBUG_CELL 116u
#define DEMO_DEBUG_CAM_X 48
#define DEMO_DEBUG_CAM_Y 144
#else
#define DEMO_DEBUG_CELL 5u
#define DEMO_DEBUG_CAM_X 0
#define DEMO_DEBUG_CAM_Y 0
#endif
#endif
#ifndef DEMO_DEBUG_CAM_X
#define DEMO_DEBUG_CAM_X 0
#endif
#ifndef DEMO_DEBUG_CAM_Y
#define DEMO_DEBUG_CAM_Y 0
#endif

constexpr eng::u16 kDebugCell = static_cast<eng::u16>(DEMO_DEBUG_CELL);
constexpr eng::s16 kDebugCamX = static_cast<eng::s16>(DEMO_DEBUG_CAM_X);
constexpr eng::s16 kDebugCamY = static_cast<eng::s16>(DEMO_DEBUG_CAM_Y);
constexpr eng::u16 kDebugAnimMaxJobs = 12u;
constexpr eng::u16 kDebugAnimMaxRows = 16u;

struct DebugAnimJob {
	eng::u32 dest;
	eng::u32 source;
	eng::u32 mask;
	eng::u32 checksum_before;
	eng::u32 checksum_source;
	eng::u32 checksum_mask;
	eng::u32 checksum_expected;
	eng::u32 checksum_after;
	eng::u16 words_per_row;
	eng::u16 height;
	eng::u16 dest_row_words;
	eng::u16 before[kDebugAnimMaxRows];
	eng::u8 kind;
	eng::u8 minterm;
	eng::u8 mismatch;
	eng::u8 pad;
};

struct DebugAnimBlit {
	eng::u32 magic;
	eng::u32 target_base;
	eng::u32 plane_stride;
	eng::u16 row_bytes;
	eng::u8 planes;
	eng::u8 job_count;
	eng::u8 mismatch_count;
	eng::u8 pending;
	eng::u16 cell;
	eng::s16 screen_x;
	eng::s16 ring_y;
	eng::s16 camx;
	eng::s16 camy;
	eng::s16 display_offset;
	DebugAnimJob jobs[kDebugAnimMaxJobs];
};
extern "C" {
__attribute__((used)) DebugAnimBlit g_debug_anim_blit {};
}
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

// Frames planares de `TILED_ANIM_TILE` (16×16 o 32×32) y máscaras 1bpp.
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


#ifndef TILED_MAP_TILE_W
#define TILED_MAP_TILE_W 16u
#endif
#ifndef TILED_MAP_TILE_H
#define TILED_MAP_TILE_H TILED_MAP_TILE_W
#endif
static_assert(TILED_MAP_TILE_W == TILED_MAP_TILE_H,
	      "la demo exige tiles cuadrados");
static_assert(TILED_ANIM_TILE == TILED_MAP_TILE_W,
	      "el frame animado debe usar el tamaño del mapa");
constexpr eng::u16 kTile = TILED_MAP_TILE_W;
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

volatile eng::u32 g_work_cycles = 0u; // CPU real del frame, sin espera de VBlank
volatile eng::u32 g_work_max = 0u;    // peor frame medido (presupuesto real)
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
	eng::playfield::TilePatchStats m_patch_stats {};      // acumulado para ENG_METRICS
	eng::playfield::TilePatchStats m_last_patch_stats {}; // último frame para el HUD J/B0..B2

	// Save-under por celda viva (fondo real bajo cada overlay). Vive en la
	// escena/demo, no en el playfield; 96 cubre el máximo visible de la visita
	// (~91 celdas en la parada de la fogata). Dos arenas: 96 × 192 B = 18 KB
	// para celdas de tile y 8 × 1,5 KB = 12 KB para sprites fusionados de hasta 8 celdas.
	eng::graphics::MapCellSaveCacheSet<96u, 8u> m_cell_save {};
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
	bool m_compose_dirty = true;
	eng::u32 m_update_start = 0u;
	eng::u32 m_update_cycles = 0u;
	eng::u32 m_render_start = 0u;
	eng::u32 m_render_cycles = 0u;
	eng::u8 m_pause_warmup = 0u;
	eng::u8 m_budget_hold = 0u; // frames de histéresis de la degradación del agua
	eng::u8 m_scroll_patch_skip = 0u; // frames que faltan para el próximo parche en scroll
	bool m_hud_pending = false;
	bool m_patch_busy = false;
	eng::u8 m_hud_busy_frames = 0u; // frames seguidos con parche pendiente (tope de refresco del HUD)
	eng::u16 m_hud_anim_group = 0u; // grupo que muestra el HUD en A= (fogata fusionada si existe)
	bool m_install_dirty = true;

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
		add_stop(kDebugCamX, kDebugCamY); // diagnóstico: una sola celda visible
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
			m_pause_warmup = 2u;
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
		// Pool de chip: bitmap corkscrew + HUD + Copper + save-under de celdas
		// vivas (96 × 16×16 × 6 planos = 18 KB). 16K slow para el resto.
		// La variante DEMO_DOUBLE_BUFFER reserva 256K para el segundo ring.
#if defined(DEMO_DOUBLE_BUFFER)
		constexpr eng::u32 kChipBytes = 256u * 1024u;
#else
		// 104K chip: el banco mixto (agua planar + flores/fogata/coberturas
		// interleaved) deja 104K contiguos en el A500 de 512K; el escenario usa
		// ~100K (anillo 69K + HUD 11,5K + save-under 18K).
		constexpr eng::u32 kChipBytes = 104u * 1024u;
#endif
		if (!backend.configure_memory({kChipBytes, 16u * 1024u, 0u})) {
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
			// Los incbin viven en la sección `.MEMF_CHIP` del asset; `as_chip`
			// certifica esa procedencia y `configured()` valida tamaños/planos.
			eng::as_chip(eng::Pattern {g_anim_sheet, TILED_ANIM_SHEET_BYTES}),
			eng::as_chip(eng::MaskBytes {g_anim_masks, TILED_ANIM_MASK_BYTES}),
			TILED_ANIM_FRAME_STRIDE, TILED_ANIM_MASK_STRIDE,
			TILED_ANIM_PLANES, TILED_ANIM_TILE);
		if (!animator.configured() || g_anim_sheet_size != TILED_ANIM_SHEET_BYTES ||
		    g_anim_masks_size != TILED_ANIM_MASK_BYTES) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Animation);
			return;
		}
		animator.set_tick(0u);
#if defined(DEMO_NO_LIVE)
		// Ablación: deja solo la base cocida (sin overlays vivos ni coberturas)
		// para separar un defecto del bake de uno del parche.
		for (eng::u8 layer = 0u; layer < animator.layer_count(); ++layer) {
			animator.set_layer_animation_enabled(layer, false);
		}
#endif
		// A= del HUD: el grupo 0 puede ser una cobertura (1 frame). Mostrar el
		// primer grupo con caja mayor que un tile, que es la fogata fusionada.
		for (eng::u16 g = 0u; g < animator.group_count(); ++g) {
			if (animator.group_pixel_width(g) > animator.tile()) {
				m_hud_anim_group = g;
				break;
			}
		}
#if defined(DEMO_DEBUG_ONE_CELL)
		animator.set_test_cell(kDebugCell);
#endif
		build_tour();
#if defined(DEMO_DEBUG_ONE_CELL)
		m_stop_index = 0u;
		m_camera_x = m_stops[0].x;
		m_camera_y = m_stops[0].y;
		m_pause_frames = 0xffffu; // congelar cámara para la celda única
#endif
#if defined(DEMO_TOUR_STOP)
		// Diagnóstico: congelar la visita en una parada concreta (0=fogata,
		// 1=agua, 2=flores, 3..=esquinas/centro) para capturar su animación.
		constexpr eng::u8 kTourStop = static_cast<eng::u8>(DEMO_TOUR_STOP);
		m_stop_index = kTourStop < m_stop_count ? kTourStop : 0u;
		m_camera_x = m_stops[m_stop_index].x;
		m_camera_y = m_stops[m_stop_index].y;
		m_pause_frames = 0xffffu;
		m_pause_warmup = 2u;
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
		scene_cfg.animator = animator; // referencia no propietaria al animador de la demo
#if defined(TILED_ANIM_COVER_LAYER) && (TILED_ANIM_COVER_LAYER != 0xffffu) && !defined(DEMO_NO_COVER)
		// Objetos estáticos que van encima de una celda viva: el parche los
		// reestampa tras pintar la celda (la banca junto a la fogata).
		// `DEMO_NO_COVER=1` es la ablación para medir su coste.
		scene_cfg.cover_layer = static_cast<eng::u8>(TILED_ANIM_COVER_LAYER);
#endif
#if defined(DEMO_DOUBLE_BUFFER)
		scene_cfg.double_buffer = true;
#endif

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
		if (!scene.pre_scroll(backend, plan, m_camera_x, m_camera_y)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Scroll);
			return;
		}
		// Dos arenas de save-under: celda = tile × tile × planos (192 B con
		// 16×16×6) y sprite fusionado = hasta 8 celdas (1,5 KB). Reservar por celda
		// fragmenta el BlockPool (64 huecos).
		const eng::u32 cell_bytes = static_cast<eng::u32>(animator.tile()) *
					    animator.tile() * animator.plane_count() / 8u; // 192 B (16×16×6)
		m_cell_save.configure(backend.memory_manager().chip(), cell_bytes,
				      cell_bytes * 8u); // 8 celdas = hasta 2×4 tiles por sprite grande
		if (!scene.draw_animation_stack(backend, plan, layer_stack, m_cell_save)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::AnimationDraw);
			return;
		}
		animator.clear_changes();
#if defined(DEMO_DOUBLE_BUFFER)
		// El fill/pre_scroll y el pase inicial de animaciones se hicieron en el
		// ring de render; volcar el frame completo al display antes de presentar.
		if (!scene.replicate_full_to_display(backend)) {
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		scene.flip();
		m_compose_dirty = true;
#endif
		if (!scene.compose()) {
			eng::debug::mark_failed(g_eng_run_status, Fail::ComposeInit);
			return;
		}
		m_compose_dirty = false;
		scene.takeover(backend);
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, kReadyDetail);
	}

#if defined(DEMO_NO_BUDGET)
	static constexpr bool kBudgetedAnimations = false; // A/B: tope fijo de 8 jobs
#else
	static constexpr bool kBudgetedAnimations = true; // prioridad por capas con histéresis
#endif

	/// Degradación por deadline de trabajo: con la ventana por encima de
	/// ~1,25 campos (41 fps) se congela el agua (capa 1, ambiente) y, si el
	/// margen se recupera (>45 fps) durante 4 frames, se reanima. La fogata y
	/// las flores (capas 2 y 0) no se degradan: son el foco visual. La espera
	/// evita oscilar en el borde del campo; el cursor round-robin del parche
	/// reparte las celdas diferidas.
	static constexpr eng::u32 kDegradeFps10 = 410u;
	static constexpr eng::u32 kRestoreFps10 = 450u;
	static constexpr eng::u8 kBudgetHold = 4u;
	/// Durante el scroll el parche solo toca la fogata y se ejecuta cada
	/// `kScrollPatchPeriod` frames: el fuego conserva ~10 fps (su cadencia de
	/// arte) y las flores/agua se quedan en su último frame. Parchear cada
	/// frame durante el scroll disparaba el coste de la tabla de visibilidad.
	static constexpr eng::u8 kScrollPatchPeriod = 5u;

	void apply_animation_budget(bool paused) {
#if defined(DEMO_NO_LIVE)
		// Ablación base-vs-vivo: las capas se quedan desactivadas del init.
		(void)paused;
		return;
#else
		if (!kBudgetedAnimations) {
			// Tope fijo de 8 jobs: en las paradas se animan las tres capas;
			// durante el scroll solo la fogata (las flores/agua se quedan en
			// el último frame, que es menos visible que congelar el fuego).
			animator.set_layer_animation_enabled(0u, paused);
			animator.set_layer_animation_enabled(1u, paused);
			animator.set_layer_animation_enabled(2u, true);
#if defined(DEMO_DISABLE_LAYER)
			animator.set_layer_animation_enabled(DEMO_DISABLE_LAYER, false);
#endif
			return;
		}
		if (!paused) {
			animator.set_layer_animation_enabled(0u, false);
			animator.set_layer_animation_enabled(1u, false);
			animator.set_layer_animation_enabled(2u, true);
#if defined(DEMO_DISABLE_LAYER)
			animator.set_layer_animation_enabled(DEMO_DISABLE_LAYER, false);
#endif
			return;
		}
		const eng::u32 fps10 = host_hud.fps10();
		if (fps10 != 0u) {
			if (fps10 < kDegradeFps10) {
				if (m_budget_hold < 8u) ++m_budget_hold;
			} else if (fps10 > kRestoreFps10) {
				if (m_budget_hold > 0u) --m_budget_hold;
			}
		}
		const bool water = m_budget_hold < kBudgetHold;
		animator.set_layer_animation_enabled(0u, true);
		animator.set_layer_animation_enabled(1u, water);
		animator.set_layer_animation_enabled(2u, true);
#endif
	}

#if defined(DEMO_DEBUG_ONE_CELL)
	/// Guarda una copia del destino de cada plano ANTES de ejecutar el plan.
	/// El checksum global anterior leía las filas de forma contigua, pero el
	/// destino está interleaved: cada fila de un plano dista row_bytes*planes.
	void capture_debug_jobs(const eng::graphics::FramePlan& plan) {
		bool has_masked = false;
		for (eng::u16 i = 0u; i < plan.blit_job_count(); ++i) {
			if (plan.blit_job(i).kind == eng::graphics::BlitJobKind::MaskedBobCookieCut) {
				has_masked = true;
				break;
			}
		}
		if (!has_masked) return;
		g_debug_anim_blit.magic = 0x414E494Du;
		g_debug_anim_blit.job_count = 0u;
		g_debug_anim_blit.mismatch_count = 0u;
		g_debug_anim_blit.pending = 1u;
		g_debug_anim_blit.cell = kDebugCell;

		const auto ctx = scene.sprite_context();
		const auto& target = ctx.targets[0];
		g_debug_anim_blit.target_base = reinterpret_cast<eng::u32>(target.planes.data());
		g_debug_anim_blit.plane_stride = target.plane_pointer_step();
		g_debug_anim_blit.row_bytes = target.row_bytes;
		g_debug_anim_blit.planes = target.plane_count;

		const eng::playfield::TileAnimationCellView& cell = animator.cell(kDebugCell);
		const eng::s32 wx = static_cast<eng::s32>(cell.x) * kTile;
		const eng::s32 wy = static_cast<eng::s32>(cell.y) * kTile;
		eng::s32 sx = (wx - scene.bg().mapposx()) % kMapW;
		if (sx < 0) sx += kMapW;
		eng::s32 sy = (wy - scene.bg().mapposy() - kTile) % kMapH;
		if (sy < 0) sy += kMapH;
		const eng::s32 ry = (scene.bg().display_offset() + sy) % scene.bg().display_height();
		g_debug_anim_blit.screen_x = static_cast<eng::s16>(sx);
		g_debug_anim_blit.ring_y = static_cast<eng::s16>(ry);
		g_debug_anim_blit.camx = static_cast<eng::s16>(scene.bg().mapposx());
		g_debug_anim_blit.camy = static_cast<eng::s16>(scene.bg().mapposy());
		g_debug_anim_blit.display_offset = static_cast<eng::s16>(scene.bg().display_offset());

		for (eng::u16 i = 0u; i < plan.blit_job_count() && i < kDebugAnimMaxJobs; ++i) {
			const eng::graphics::BlitJob& job = plan.blit_job(i);
			if (job.kind != eng::graphics::BlitJobKind::MaskedBobCookieCut) continue;
			if (g_debug_anim_blit.job_count >= kDebugAnimMaxJobs) break;

			DebugAnimJob& out = g_debug_anim_blit.jobs[g_debug_anim_blit.job_count];
			out = {};
			out.dest = reinterpret_cast<eng::u32>(job.destination.cwords());
			out.source = reinterpret_cast<eng::u32>(job.source.cwords());
			out.mask = reinterpret_cast<eng::u32>(job.mask.cwords());
			out.words_per_row = job.words_per_row;
			out.height = job.height;
			out.kind = static_cast<eng::u8>(job.kind);
			out.minterm = static_cast<eng::u8>(job.minterm);

			const eng::u16 dest_row_words = static_cast<eng::u16>(
				job.destination_plane_stride_bytes / 2u);
			out.dest_row_words = dest_row_words;
			const eng::u16* dest = job.destination.cwords();
			const eng::u16* src = job.source.cwords();
			const eng::u16* msk = job.mask.cwords();
			if (dest == nullptr || src == nullptr || msk == nullptr ||
			    dest_row_words == 0u || job.words_per_row != 1u ||
			    job.height > kDebugAnimMaxRows) {
				out.mismatch = 1u;
				++g_debug_anim_blit.mismatch_count;
				++g_debug_anim_blit.job_count;
				continue;
			}
			for (eng::u16 row = 0u; row < job.height; ++row) {
				const eng::u16* dest_row = dest + static_cast<eng::u32>(row) * dest_row_words;
				const eng::u16 before = *dest_row;
				const eng::u16 source = src[row];
				const eng::u16 mask = msk[row];
				out.before[row] = before;
				out.checksum_before += before;
				out.checksum_source += source;
				out.checksum_mask += mask;
				out.checksum_expected +=
					(static_cast<eng::u32>(source) & mask) |
					(static_cast<eng::u32>(before) & (~static_cast<eng::u32>(mask) & 0xffffu));
			}
			out.checksum_after = 0u;
			++g_debug_anim_blit.job_count;
		}
	}

	/// Compara cada fila después del blit con el resultado del cookie-cut.
	void verify_debug_jobs() {
		if (g_debug_anim_blit.magic != 0x414E494Du || g_debug_anim_blit.pending == 0u) return;
		g_debug_anim_blit.pending = 0u;
		for (eng::u8 j = 0u; j < g_debug_anim_blit.job_count; ++j) {
			DebugAnimJob& job = g_debug_anim_blit.jobs[j];
			if (job.mismatch != 0u) continue;
			const eng::u16 dest_row_words = job.dest_row_words;
			const eng::u16* dest = reinterpret_cast<const eng::u16*>(job.dest);
			const eng::u16* src = reinterpret_cast<const eng::u16*>(job.source);
			const eng::u16* msk = reinterpret_cast<const eng::u16*>(job.mask);
			if (dest == nullptr || src == nullptr || msk == nullptr ||
			    dest_row_words == 0u || job.words_per_row != 1u) {
				job.mismatch = 1u;
				++g_debug_anim_blit.mismatch_count;
				continue;
			}
			eng::u32 sum = 0u;
			for (eng::u16 row = 0u; row < job.height; ++row) {
				const eng::u16* dest_row = dest + static_cast<eng::u32>(row) * dest_row_words;
				const eng::u16 actual = *dest_row;
				const eng::u32 expected =
					(static_cast<eng::u32>(src[row]) & msk[row]) |
					(static_cast<eng::u32>(job.before[row]) &
					 (~static_cast<eng::u32>(msk[row]) & 0xffffu));
				sum += actual;
				if (actual != expected) job.mismatch = 1u;
			}
			job.checksum_after = sum;
			if (job.mismatch != 0u) ++g_debug_anim_blit.mismatch_count;
		}
	}
#endif

	void update(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		if (!ready) return;
		m_update_start = eng::debug::DebugPeripheral::cycle_counter();
		DEMO_PROF_FRAME();
		eng::debug::metrics.begin_frame(context.frame.frame_index);
		DEMO_PROF_BEGIN(0u);
		plan.clear();
		plan.set_blit_budget_limits({16384, 65536, 120, 512});
		eng::s16 tour_dx = 0, tour_dy = 0;
		update_tour(tour_dx, tour_dy);
		const bool tour_paused = m_pause_frames != 0u;
		if (tour_paused) m_scroll_patch_skip = 0u;
		apply_animation_budget(tour_paused);
		animator.set_tick(context.frame.frame_index);
		// M de régimen: el primer frame de la parada incluye la pasada completa
		// de llegada. Se mide, y desde el segundo frame se reinicia para que M
		// describa el coste estable de la animación, no la transición.
		if (tour_paused) {
			if (m_pause_warmup == 2u) {
				m_pause_warmup = 1u;
			} else if (m_pause_warmup == 1u) {
				g_work_max = 0u;
				m_pause_warmup = 0u;
			}
		} else {
			m_pause_warmup = 0u;
		}
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
#if defined(DEMO_DOUBLE_BUFFER)
		if (!scene.replicate_to_display(backend, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
#endif
		DEMO_PROF_END(0u);
		DEMO_PROF_BEGIN(1u);
		plan.clear();
		// Tope del parche: 8 jobs por frame = el grupo mayor (4 celdas con máscara
		// variable × restore+frame; la captura inicial sustituye al restore, no
		// suma). Las celdas de silueta fija (agua) cuestan un blit. Con 6 el
		// grupo de 4 no cabe nunca y `pass_deferred` no termina; con 12 medido
		// peor: más jobs por frame suben el pico sin reducir el trabajo total.
		// El parche se ejecuta también durante el scroll: la política de capas
		// deja activa solo la fogata (las flores/agua se quedan en su último
		// frame) y el pase se repite cada `kScrollPatchPeriod` frames para que
		// el fuego siga sin pagar la tabla de visibilidad en cada frame.
		plan.set_blit_budget_limits({16384, 65536, 8, 8});
		const bool patch_now = m_scroll_patch_skip == 0u;
		if (patch_now) {
#if defined(DEMO_BLIT_STATS) || defined(ENG_METRICS)
			m_patch_stats = {};
#if defined(DEMO_NO_PATCH)
			const bool patch_ok = true;
#else
			const bool patch_ok = scene.patch_animations(plan, 0u, m_cell_save, &m_patch_stats);
#endif
#else
#if defined(DEMO_NO_PATCH)
			const bool patch_ok = true;
#else
			const bool patch_ok = scene.patch_animations(plan, 0u, m_cell_save);
#endif
#endif
			if (!patch_ok) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::AnimationDraw);
				return;
			}
			// El HUD conserva las últimas estadísticas de un frame con trabajo:
			// si este frame no cambió ninguna celda, B0/B1/B2 no debe parecer
			// que el parche dejó de pintar.
			if (plan.blit_job_count() != 0u) m_last_patch_stats = m_patch_stats;
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
			if (!tour_paused) m_scroll_patch_skip = kScrollPatchPeriod - 1u;
		} else if (m_scroll_patch_skip != 0u) {
			--m_scroll_patch_skip;
		}
#if defined(DEMO_DEBUG_ONE_CELL)
		capture_debug_jobs(plan);
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
		m_patch_busy = plan.blit_job_count() != 0u;
		DEMO_PROF_BEGIN(2u);
		if (plan.blit_job_count() != 0u && !backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
#if defined(DEMO_DOUBLE_BUFFER)
		if (!scene.replicate_to_display(backend, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, Fail::Blitter);
			return;
		}
		scene.flip();
		m_compose_dirty = true;
#endif
#if defined(DEMO_DEBUG_ONE_CELL)
		verify_debug_jobs();
#endif
		DEMO_PROF_END(2u);
		DEMO_PROF_BEGIN(3u);
		// Con la cámara quieta y sin flip, la copperlist no cambia: el parche
		// escribe en el bitmap, no en los registros de scroll. Recomponer cada
		// frame era ~51k ciclos de CPU sin ningún trabajo visible.
		if (m_compose_dirty || tour_dx != 0 || tour_dy != 0) {
			if (!scene.compose()) {
				ready = false;
				eng::debug::mark_failed(g_eng_run_status, Fail::ComposeFrame);
				return;
			}
			m_compose_dirty = false;
			m_install_dirty = true;
		}
		DEMO_PROF_END(3u);
		DEMO_PROF_BEGIN(4u);
		// El HUD de host cambia de frame cada VBlank, pero rehacer su texto
		// cuesta ~52k ciclos. Se refresca cada 4 frames y se evita en los frames
		// de parche para no subir el pico; si el parche encadena más de 8 frames
		// (parada de la fogata con agua/objetos) se fuerza uno para que el
		// contador de frames/fps no quede congelado.
		constexpr eng::u8 kHudMaxBusyFrames = 8u;
		if ((context.frame.frame_index & 3u) == 0u) m_hud_pending = true;
		if (m_patch_busy) {
			if (m_hud_busy_frames < kHudMaxBusyFrames) ++m_hud_busy_frames;
		} else {
			m_hud_busy_frames = 0u;
		}
		if (m_hud_pending && (!m_patch_busy || m_hud_busy_frames >= kHudMaxBusyFrames)) {
			m_hud_busy_frames = 0u;
		eng::util::TextLine<96> stats;
		stats << "X=" << scene.bg().mapposx() << " Y=" << scene.bg().mapposy()
		      << " A=" << animator.frame_of(m_hud_anim_group) << " G=" << animator.group_count()
		      << " W=" << g_work_cycles << " M=" << g_work_max;
#if defined(DEMO_PROF)
		// Coste medio por frame de las secciones del demo (miles de ciclos):
		// 0=scroll+execute, 1=parche (construcción), 2=parche (execute),
		// 3=compose, 4=HUD. Diagnóstico de reparto, no camino de producción.
		const eng::u32 pf = eng::debug::g_eng_prof.frames != 0u
			? eng::debug::g_eng_prof.frames : 1u;
		stats << " P=" << (eng::debug::g_eng_prof.cycles[0] / pf / 1000u) << '/'
		      << (eng::debug::g_eng_prof.cycles[1] / pf / 1000u) << '/'
		      << (eng::debug::g_eng_prof.cycles[2] / pf / 1000u) << '/'
		      << (eng::debug::g_eng_prof.cycles[3] / pf / 1000u) << '/'
		      << (eng::debug::g_eng_prof.cycles[4] / pf / 1000u)
		      << " Q=" << (eng::debug::g_eng_prof.max_cycles[1] / 1000u)
		      << " N=" << eng::debug::g_eng_prof.calls[1];
#endif
#if defined(DEMO_BLIT_STATS)
		stats << " J=" << budget.jobs << " B0=" << m_last_patch_stats.cells_patched_by_layer[0]
		      << " B1=" << m_last_patch_stats.cells_patched_by_layer[1]
		      << " B2=" << m_last_patch_stats.cells_patched_by_layer[2]
		      << " S=" << m_cell_save.used() << " F=" << m_cell_save.failures();
#if defined(DEMO_DEBUG_ONE_CELL)
		stats << " M=" << g_debug_anim_blit.mismatch_count
		      << " N=" << g_debug_anim_blit.job_count;
#endif
#endif
		host_hud.draw(backend.debug(), "207 TILED LIVE", kVersion, context.frame.frame_index,
			      eng::debug::prof_clock(), stats.c_str());
		m_hud_pending = false;
		}
		DEMO_PROF_END(4u);
		m_update_cycles = eng::debug::DebugPeripheral::cycle_counter() - m_update_start;
	}
	void render(eng::amiga::AmigaBackend& backend, eng::GameContext& context) {
		m_render_start = eng::debug::DebugPeripheral::cycle_counter();
		if (ready && m_install_dirty) {
			scene.install(backend);
			m_install_dirty = false;
		}
		m_render_cycles = eng::debug::DebugPeripheral::cycle_counter() - m_render_start;
		g_work_cycles = m_update_cycles + m_render_cycles;
		if (g_work_cycles > g_work_max) g_work_max = g_work_cycles;
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
	// Toma de control del SO: el bucle IRQ puede usar Wait() sin que el
	// planificador de AmigaDOS apague el latido de VBlank.
	if (!backend.takeover_system()) return 0;
	engine.run_frames(0xffffu);
	return 0;
}
