#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
#include <eng/graphics/field_controller.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/core/span.hpp>

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

namespace {
namespace drivers = eng::graphics::drivers;
using Scene = drivers::TileScrollScene<drivers::TileScrollMode::dual(3, 3), 24, 20>;
using Field = eng::graphics::TileFieldController<Scene>;

static_assert(Scene::surface_width == 704 && Scene::surface_height == 576);
constexpr eng::u16 kTileSize = Scene::tile_size;
constexpr eng::u16 kMapWidth = 256;
constexpr eng::u16 kMapHeight = 128;
constexpr eng::u8 kBackground = 0;
constexpr eng::u8 kForeground = 1;

constexpr drivers::EhbPalette kPalette {{
	0x000, 0xf24, 0xf90, 0xff0, 0x0cf, 0x84f, 0xf4c, 0xfff,
	0x000, 0x013, 0x057, 0x08a, 0x0ad, 0x2d8, 0x8fc, 0xdff,
}};
constexpr drivers::EhbPaletteZone kZones[] {};

constexpr eng::u32 hash(eng::u32 value) {
	value ^= value >> 16u; value *= 0x7feb352du; value ^= value >> 15u; return value;
}

/// Mapas editables: cada campo tiene su propio almacenamiento y sus propios datos.
template <eng::u16 Width, eng::u16 Height>
struct PrefilledMap {
	eng::u16 cells[static_cast<eng::u32>(Width) * Height] {};
	void generate(eng::u32 seed) {
		for (eng::u16 y = 0; y < Height; ++y) for (eng::u16 x = 0; x < Width; ++x)
			cells[static_cast<eng::u32>(y) * Width + x] = static_cast<eng::u16>(hash(seed + (static_cast<eng::u32>(y) << 16u) + x) & 15u);
	}
};

constexpr eng::u8 hex_glyph_row(eng::u8 glyph, eng::u8 row) {
	constexpr eng::u8 glyphs[] {
		0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e,
		0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e,
		0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f,
		0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e,
		0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02,
		0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e,
		0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e,
		0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08,
		0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e,
		0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e,
		0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11,
		0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e,
		0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e,
		0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e,
		0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f,
		0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10,
	};
	return glyphs[static_cast<eng::u16>(glyph & 15u) * 7u + row % 7u];
}

constexpr eng::u16 tile_row(eng::u8 tile, eng::u8 row, eng::u8 plane, bool foreground) {
	// El color se calcula una sola vez por pixel logico; solo su bit `plane`
	// cambia entre los tres words. Variarlo por plano produciria colores que no
	// corresponden al indice del tile y dificulta comprobar visualmente el DPF.
	const eng::u8 background = static_cast<eng::u8>(1u + ((tile >> 2u) + 3u) % 7u);
	const eng::u8 border_colour = static_cast<eng::u8>(1u + ((tile + 5u) % 7u));
	const eng::u8 glyph_colour = static_cast<eng::u8>(1u + ((tile + 2u) % 7u));
	const eng::u16 border = (row == 0u || row == 15u) ? 0xffffu : 0x8001u;
	eng::u16 glyph = 0;
	if (row >= 1u && row <= 14u) {
		const eng::u8 glyph_bits = hex_glyph_row(tile, static_cast<eng::u8>((row - 1u) / 2u));
		for (eng::u8 x = 0; x < 5u; ++x) {
			if ((glyph_bits & (1u << (4u - x))) != 0u) {
				glyph |= static_cast<eng::u16>(0xc000u >> (x * 2u + 2u));
			}
		}
	}
	const eng::u16 interior = static_cast<eng::u16>(~(border | glyph));
	const eng::u16 checker = (row & 1u) == 0u ? 0xaaaa : 0x5555;
	const eng::u16 background_pixels = static_cast<eng::u16>(
		foreground ? interior & checker : interior);
	return static_cast<eng::u16>(
		((border_colour & (1u << plane)) != 0u ? border : 0u) |
		((glyph_colour & (1u << plane)) != 0u ? glyph : 0u) |
		((background & (1u << plane)) != 0u ? background_pixels : 0u));
}

struct TileSet {
	eng::MemoryBlock memory {};
	eng::u8 planes = 0;
	bool init(eng::amiga::MinimalBackend& backend, eng::u8 playfield) {
		planes = Scene::playfield_planes(playfield);
		memory = backend.memory().chip.allocate(Scene::playfield_tile_bytes(playfield) * 16u, 16);
		if (!memory.valid()) return false;
		eng::Span<eng::u16> words = eng::Span<eng::u16>::from_raw(static_cast<eng::u16*>(memory.data), memory.size / sizeof(eng::u16));
		for (eng::u8 tile = 0; tile < 16; ++tile) for (eng::u8 plane = 0; plane < planes; ++plane)
			for (eng::u8 row = 0; row < kTileSize; ++row)
				words.at((static_cast<eng::u32>(tile) * planes + plane) * kTileSize + row) = tile_row(tile, row, plane, playfield == kForeground);
		return true;
	}
};

struct DemoGame {
	Scene scene {};
	PrefilledMap<kMapWidth, kMapHeight> maps[2] {};
	TileSet tiles[2] {};
	Field fields[2] {};
	eng::graphics::FramePlan plan {};
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({384u * 1024u, 8u * 1024u, 8u * 1024u}) ||
			!scene.init(backend.memory(), {&kPalette, kZones, 0, 1536}) ||
			!tiles[0].init(backend, kBackground) || !tiles[1].init(backend, kForeground)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010501u); return;
		}
		maps[0].generate(0x10500000u); maps[1].generate(0x51a50000u);
		for (eng::u8 pf = 0; pf < 2; ++pf) {
			eng::graphics::TileFieldConfig config {
				eng::graphics::TileFieldSource::TileMap,
				{maps[pf].cells, kMapWidth, kMapHeight},
				{},
				static_cast<const eng::u16*>(tiles[pf].memory.data), 16, tiles[pf].planes, pf, 4, true,
			};
			fields[pf].configure(config);
			if (!fields[pf].begin(scene)) { eng::debug::mark_failed(g_eng_run_status, 0x00010501u); return; }
		}
		set_copper(); scene.install(backend); ready = true;
		eng::debug::mark_ready(g_eng_run_status, 0x10500000u);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) return;
		plan.clear();
		// El presupuesto total protege al FramePlan; cada campo aplica ademas su cupo local.
		plan.set_blit_budget_limits({96, 128, 2, 8});
		for (eng::u8 pf = 0; pf < 2; ++pf)
			if (!fields[pf].update(scene, plan, static_cast<eng::u16>(context.frame.frame_index + pf))) {
				ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00010502u); return;
			}
		if (!backend.execute_frame_plan(plan) || !set_copper()) {
			ready = false; eng::debug::mark_failed(g_eng_run_status, 0x00010502u); return;
		}
		const eng::u8 page0 = fields[0].state().page(static_cast<eng::u16>(fields[0].state().x / kTileSize / 20u), static_cast<eng::u16>(fields[0].state().y / kTileSize / 16u));
		const eng::u8 page1 = fields[1].state().page(static_cast<eng::u16>(fields[1].state().x / kTileSize / 20u), static_cast<eng::u16>(fields[1].state().y / kTileSize / 16u));
		const eng::u32 marker = 0x10500000u | (static_cast<eng::u32>(page0) << 16u) | (static_cast<eng::u32>(page1) << 18u) |
			(static_cast<eng::u32>(fields[0].state().uploads + fields[1].state().uploads) & 0xffffu);
		eng::debug::mark_ready(g_eng_run_status, marker);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (ready) scene.install(backend);
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	private:
	bool set_copper() {
		drivers::TileScrollInput input {};
		for (eng::u8 pf = 0; pf < 2; ++pf) {
			input.playfield[pf] = fields[pf].scroll_position();
			input.page[pf] = fields[pf].page_origin();
		}
		return scene.rebuild_copper(input);
	}
};

DemoGame game {};
}

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);
	eng::amiga::MinimalBackend backend {};
	eng::Engine engine {backend, game};
	engine.run_frames(0xffffffffu);
	return 0;
}
