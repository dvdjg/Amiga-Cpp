#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/field/dpf_composer.hpp>
#include <eng/field/tile_demo.hpp>
#include <eng/field/tile_field.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/core/span.hpp>

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
namespace demo = eng::field::demo;

/// Selección de configuración por parámetros de compilación (-D):
///   -DK_TILE_WIDTH=16|32      ancho de tile en px (múltiplo de 16).
///   -DK_DUAL=1|0              1 => dual playfield 3+3 (6 planos),
///                              0 => single playfield 5 planos (32 colores).
/// La demo es el SHOWCASE del playfield universal: un controlador por campo,
/// dual o single, tiles anchos o normales, scroll infinito en ambos ejes.
#ifndef K_TILE_WIDTH
#define K_TILE_WIDTH 16
#endif
#ifndef K_DUAL
#define K_DUAL 1
#endif

constexpr eng::u16 kTileWidth = static_cast<eng::u16>(K_TILE_WIDTH);
constexpr bool kDual = (K_DUAL != 0);

constexpr eng::u8 kPlanes = kDual ? 6 : 5;
constexpr eng::u8 kFgPlanes = kDual ? 3 : 0;
constexpr eng::u8 kBgPlanes = kDual ? 3 : 5;
constexpr eng::u8 kBackground = kDual ? 1 : 0;  // PF2 (atrás) en dual; en single no hay 2º PF
constexpr eng::u8 kForeground = 0;              // PF1: delante (dual) o único (single)

constexpr eng::u16 kTileSize = 16;
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u16 kMapTilesX = 256;
constexpr eng::u16 kMapTilesY = 128;
constexpr eng::u8 kTileCount = 64;

eng::u16 bg_map_cells[kMapTilesX * kMapTilesY] {};
eng::u16 fg_map_cells[kMapTilesX * kMapTilesY] {};

/// Rellena un mapa (índices u16) con patrones derivados de una semilla.
void build_map(eng::Span<eng::u16> cells, bool is_foreground, eng::u32 seed) {
	const eng::u32 layer_seed = seed ^ (is_foreground ? 0xf0f0f0f0u : 0x0f0f0f0fu);
	for (eng::u32 y = 0; y < kMapTilesY; ++y) {
		for (eng::u32 x = 0; x < kMapTilesX; ++x) {
			const eng::u32 h = demo::cell_hash(x, y, layer_seed);
			eng::u16 tile = static_cast<eng::u16>(h & 63u);
			if (is_foreground && ((h >> 9u) & 1u) != 0u) {
				tile = 63u; // totalmente transparente: el fondo se ve a través
			}
			cells.at(y * kMapTilesX + x) = tile;
		}
	}
}

struct DemoGame {
	eng::MemoryBlock tiles_bg {};
	eng::MemoryBlock tiles_fg {};
	field::TileFieldController bg {};
	field::TileFieldController fg {};
	field::DpfDisplayComposer composer {};
	eng::graphics::FramePlan plan {};
	field::TileFieldConfig bg_config {};
	field::TileFieldConfig fg_config {};
	eng::u32 tiles_uploaded = 0;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		// La superficie compacta reserva viewport + dos bloques por eje activo.
		if (!backend.configure_memory({360u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010601u);
			return;
		}

		// Tilesets por campo (banco propio de patrones en Chip RAM).
		const eng::u32 words_per_tile = kTileSize * (kTileWidth / 16u);
		tiles_bg = backend.memory().chip.allocate(kTileCount * kBgPlanes * words_per_tile * 2u, 16);
		if (kDual) {
			tiles_fg = backend.memory().chip.allocate(kTileCount * kFgPlanes * words_per_tile * 2u, 16);
		}
		if (!tiles_bg.valid() || (kDual && !tiles_fg.valid())) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010602u);
			return;
		}
		demo::build_tile_cache(tile_span(tiles_bg), kTileCount, kTileSize, kTileWidth, kBgPlanes,
			kDual ? 8 : 0, kDual ? false : true, -1);
		if (kDual) {
			demo::build_tile_cache(tile_span(tiles_fg), kTileCount, kTileSize, kTileWidth, kFgPlanes,
				0, true, 63);
		}

		build_map(eng::Span<eng::u16>::from_raw(bg_map_cells, kMapTilesX * kMapTilesY), false, 0x13579bdu);
		if (kDual) {
			build_map(eng::Span<eng::u16>::from_raw(fg_map_cells, kMapTilesX * kMapTilesY), true, 0x2468aceu);
		}

		bg_config = make_config(tiles_bg, kBgPlanes, false);
		if (kDual) {
			fg_config = make_config(tiles_fg, kFgPlanes, true);
		}

		// Estampado inicial por Blitter, en lotes hasta terminar.
		const eng::u8 budget = 120;
		if (!begin_field(backend, bg, bg_config)) {
			return;
		}
		if (kDual && !begin_field(backend, fg, fg_config)) {
			return;
		}

		if (!composer.init(backend.memory(), {
			demo::kPalette, 1536, kDual, false, kPlanes,
		})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010607u);
			return;
		}

		plan.clear();
		const field::FieldHardwareView v_fg = kDual ? fg.hardware_view(kForeground) : field::FieldHardwareView{};
		const field::FieldHardwareView v_bg = kDual
			? bg.hardware_view(kBackground)
			: bg.hardware_view(kForeground);
		if (!composer.compose(kDual ? v_fg : v_bg, v_bg)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010608u);
			return;
		}
		composer.install(backend);

		eng::debug::DebugPeripheral::counter_name(0, reinterpret_cast<eng::u32>("tiles_uploaded"));
		ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x10600000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) {
			return;
		}
		const eng::u32 frame = context.frame.frame_index;

		// Cámaras: fondo diagonal infinita, primer plano Lissajous (Q16).
		const demo::CameraQ16 fg_q = demo::fg_lissajous_camera(frame, kViewportW, kViewportH, 640, 480);
		const eng::s32 bg_x = demo::bg_scroll_x(frame);
		const eng::s32 bg_y = demo::bg_scroll_y(frame);

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 120});

		const field::TileScrollOffset bg_delta {
			static_cast<eng::s16>(bg_x - bg_last_x),
			static_cast<eng::s16>(bg_y - bg_last_y),
		};
		bg_last_x = bg_x;
		bg_last_y = bg_y;

		// Delta del fg en sub-píxeles con acumulador de resto (movimiento suave).
		fg_rest_x += fg_q.x - fg_last_x;
		fg_rest_y += fg_q.y - fg_last_y;
		fg_last_x = fg_q.x;
		fg_last_y = fg_q.y;
		const eng::s16 fg_dx = static_cast<eng::s16>(fg_rest_x / 65536);
		const eng::s16 fg_dy = static_cast<eng::s16>(fg_rest_y / 65536);
		fg_rest_x -= static_cast<eng::s32>(fg_dx) * 65536;
		fg_rest_y -= static_cast<eng::s32>(fg_dy) * 65536;
		const field::TileScrollOffset fg_delta {fg_dx, fg_dy};

		if (!bg.update(bg_config, bg_delta, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00010609u);
			return;
		}
		if (kDual && !fg.update(fg_config, fg_delta, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0001060au);
			return;
		}
		tiles_uploaded += plan.blit_budget().tile_jobs;
		eng::debug::DebugPeripheral::counter_value(0, tiles_uploaded);

		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0001060bu);
			return;
		}

		const field::FieldHardwareView v_fg = kDual ? fg.hardware_view(kForeground) : field::FieldHardwareView{};
		const field::FieldHardwareView v_bg = kDual
			? bg.hardware_view(kBackground)
			: bg.hardware_view(kForeground);
		if (!composer.compose(kDual ? v_fg : v_bg, v_bg)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0001060cu);
			return;
		}

		// Telemetría: mundo del fg/bg en tiles (bits 8-15 X, 0-7 Y).
		const eng::u32 marker = 0x10600000u |
			(static_cast<eng::u32>((fg_q.x >> 16 >> 4) & 0xffu) << 8u) |
			static_cast<eng::u32>((fg_q.y >> 16 >> 4) & 0xffu);
		eng::debug::mark_ready(g_eng_run_status, marker);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) {
			composer.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static eng::Span<eng::u16> tile_span(const eng::MemoryBlock& block) {
		return eng::Span<eng::u16>::from_raw(
			static_cast<eng::u16*>(block.data),
			block.size / sizeof(eng::u16)
		);
	}

	bool begin_field(eng::amiga::MinimalBackend& backend, field::TileFieldController& ctl, const field::TileFieldConfig& cfg) {
		const eng::u8 budget = 120;
		if (!ctl.begin(backend.memory(), cfg, {0, 0})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010603u);
			ready = false;
			return false;
		}
		while (ctl.busy()) {
			plan.clear();
			if (!ctl.pump(plan, budget)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00010605u);
				ready = false;
				return false;
			}
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00010604u);
				ready = false;
				return false;
			}
		}
		plan.clear();
		return true;
	}

	field::TileFieldConfig make_config(const eng::MemoryBlock& tiles, eng::u8 planes, bool is_foreground) {
		field::TileFieldConfig config {};
		config.map.cells = is_foreground
			? eng::Span<const eng::u16>::from_raw(fg_map_cells, kMapTilesX * kMapTilesY)
			: eng::Span<const eng::u16>::from_raw(bg_map_cells, kMapTilesX * kMapTilesY);
		config.map.width = kMapTilesX;
		config.map.height = kMapTilesY;
		config.map.wrap_x = kMapTilesX;
		config.map.wrap_y = kMapTilesY;
		config.map.edge_tile = 63;
		config.tileset = static_cast<const eng::u16*>(tiles.data);
		config.tileset_count = kTileCount;
		config.tileset_planes = planes;
		config.tile_width = kTileWidth;
		config.tile_size = kTileSize;
		config.viewport_w = kViewportW;
		config.viewport_h = kViewportH;
		config.max_delta_x = 5;
		config.max_delta_y = 5;
		config.max_tiles_per_frame = 56;
		config.safety_margin_blocks = 2;
		config.scroll_x = true;
		config.scroll_y = true;
		return config;
	}

	eng::s32 bg_last_x = 0;
	eng::s32 bg_last_y = 0;
	eng::s32 fg_last_x = 0;
	eng::s32 fg_last_y = 0;
	eng::s32 fg_rest_x = 0;
	eng::s32 fg_rest_y = 0;
};

DemoGame g_game {};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	eng::Engine engine { backend, g_game };
	engine.run_frames(0xffffffffu);

	return 0;
}
