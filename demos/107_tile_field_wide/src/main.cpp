#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/field/dpf_composer.hpp>
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

/// Single playfield de 4 bitplanes (16 colores), tiles de 32px de ancho.
/// Este test valida la optimización de tiles ANCHOS: `tile_width=32` hace que
/// `make_tile_copy_job` genere words_per_row=2 y copie cada tile en UNA pasada
/// del Blitter (no en pasadas de 16px).
constexpr eng::u8 kPlanes = 4;
constexpr eng::u16 kTileSize = 16;
constexpr eng::u16 kTileWidth = 32;   // MÚLTIPLO DE 16
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u16 kMapTilesX = 64;
constexpr eng::u16 kMapTilesY = 32;
constexpr eng::u8 kTileCount = 64;

eng::u16 map_cells[kMapTilesX * kMapTilesY] {};

constexpr eng::u16 palette[32] {
	0x000, 0xf00, 0x0f0, 0x00f, 0xff0, 0xf0f, 0x0ff, 0xfff,
	0x000, 0x800, 0x080, 0x008, 0x880, 0x808, 0x088, 0x888,
};

constexpr eng::u32 cell_hash(eng::u32 x, eng::u32 y, eng::u32 seed) {
	eng::u32 h = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	h *= 0x846ca68bu;
	h ^= h >> 16u;
	return h;
}

void build_map(eng::Span<eng::u16> cells) {
	for (eng::u32 y = 0; y < kMapTilesY; ++y) {
		for (eng::u32 x = 0; x < kMapTilesX; ++x) {
			const eng::u32 h = cell_hash(x, y, 0x1070c0deu);
			cells.at(y * kMapTilesX + x) = static_cast<eng::u16>(h & 63u);
		}
	}
}

/// Glifo hexadecimal 5x7 (para distinguir tiles).
constexpr eng::u8 hex_glyph_row(eng::u8 glyph, eng::u8 row) {
	constexpr eng::u8 rows[] {
		0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e, // 0
		0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e, // 1
		0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f, // 2
		0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e, // 3
		0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02, // 4
		0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e, // 5
		0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e, // 6
		0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, // 7
		0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e, // 8
		0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e, // 9
		0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, // A
		0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, // B
		0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, // C
		0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e, // D
		0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f, // E
		0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10, // F
	};
	return rows[static_cast<eng::u16>(glyph & 0x0fu) * 7u + (row % 7u)];
}

/// Fila PLANAR de 32px (2 words) de un tile ancho.
///
/// Mitad izquierda: glifo + borde. Mitad derecha: patron de variante (para
/// distinguir que AMBAS words se copian). Cada word va a un plano distinto
/// segun el color del pixel.
constexpr eng::u16 wide_plane_word(eng::u8 tile, eng::u8 row, eng::u8 plane, bool right_half) {
	const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
	const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
	const eng::u8 color = static_cast<eng::u8>(1u + variant); // 1..4
	if (!right_half) {
		// Mitad izquierda: borde + glifo, como los tiles de 16px.
		const eng::u16 border = (row == 0u || row == 15u) ? 0xffffu : 0x8001u;
		eng::u16 glyph_mask = 0;
		if (row >= 1u && row <= 14u) {
			const eng::u8 bits = hex_glyph_row(glyph, static_cast<eng::u8>((row - 1u) / 2u));
			for (eng::u8 x = 0; x < 5u; ++x) {
				if ((bits & (1u << (4u - x))) != 0u) {
					glyph_mask |= static_cast<eng::u16>(0xc000u >> (x * 2u + 2u));
				}
			}
		}
		const eng::u16 interior = static_cast<eng::u16>(~(border | glyph_mask));
		eng::u16 word = 0;
		if ((color & (1u << plane)) != 0u) {
			word |= interior;
		}
		if ((7u & (1u << plane)) != 0u) {
			word |= border;
		}
		if ((6u & (1u << plane)) != 0u) {
			word |= glyph_mask;
		}
		return word;
	}
	// Mitad derecha: patron de variante (barras), distinguible visualmente.
	const eng::u16 stripe = (row + variant) % 2u == 0u ? 0xaaaa : 0x5555;
	eng::u16 word = 0;
	if ((color & (1u << plane)) != 0u) {
		word |= 0xffffu;
	}
	if ((8u & (1u << plane)) != 0u) {
		word ^= stripe;
	}
	return word;
}

/// Construye el tileset con tiles de 32px: [tile][plano][16 filas x 2 words].
void build_tile_cache(eng::Span<eng::u16> words) {
	const eng::u32 words_per_tile = words.size() / kTileCount;
	const eng::u32 words_per_plane = kTileSize * (kTileWidth / 16u); // 32 words
	for (eng::u16 tile = 0; tile < kTileCount; ++tile) {
		for (eng::u8 plane = 0; plane < kPlanes; ++plane) {
			for (eng::u8 row = 0; row < kTileSize; ++row) {
				const eng::u32 base = static_cast<eng::u32>(tile) * words_per_tile +
					static_cast<eng::u32>(plane) * words_per_plane +
					static_cast<eng::u32>(row) * (kTileWidth / 16u);
				words.at(base + 0u) = wide_plane_word(tile, row, plane, false);
				words.at(base + 1u) = wide_plane_word(tile, row, plane, true);
			}
		}
	}
}

/// Seno de 64 pasos (amplitud 64) + interpolación Q16 para movimiento suave.
constexpr eng::s16 sin64(eng::u8 index) {
	constexpr eng::s16 t[] {
		0, 6, 12, 18, 24, 31, 36, 41,
		45, 49, 53, 56, 59, 61, 63, 64,
		64, 64, 63, 61, 59, 56, 53, 49,
		45, 41, 36, 31, 24, 18, 12, 6,
		0, -6, -12, -18, -24, -31, -36, -41,
		-45, -49, -53, -56, -59, -61, -63, -64,
		-64, -64, -63, -61, -59, -56, -53, -49,
		-45, -41, -36, -31, -24, -18, -12, -6,
	};
	return t[index & 63u];
}

constexpr eng::s32 sin_smooth(eng::u32 frame_index, eng::u32 period) {
	const eng::u32 ph = (frame_index * 4096u) / period;
	const eng::u8 i = static_cast<eng::u8>((ph >> 6) & 63u);
	const eng::u8 frac = static_cast<eng::u8>(ph & 63u);
	const eng::s32 s0 = sin64(i);
	const eng::s32 s1 = sin64(static_cast<eng::u8>(i + 1u) & 63u);
	return (s0 * static_cast<eng::s32>(64 - frac) + s1 * static_cast<eng::s32>(frac)) * 1024;
}

struct ScrollPosQ16 {
	eng::s32 x = 0;
	eng::s32 y = 0;
};

/// Lissajous de 2 pantallas en X (0..640 px) y 1 en Y, con precisión Q16.
constexpr ScrollPosQ16 wave_camera(eng::u32 frame_index) {
	const eng::s32 sx = sin_smooth(frame_index + 3u * 640u / 4u, 640) * 320 / 64;
	const eng::s32 sy = sin_smooth(frame_index + 3u * 480u / 4u, 480) * 256 / 64;
	return {320 * 65536 + sx, 256 * 65536 + sy};
}

struct DemoGame {
	eng::MemoryBlock tiles {};
	field::TileFieldController field_ctrl {};
	field::DpfDisplayComposer composer {};
	eng::graphics::FramePlan plan {};
	field::TileFieldConfig config {};
	ScrollPosQ16 last {};
	eng::s32 rest_x = 0;
	eng::s32 rest_y = 0;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({280u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010701u);
			return;
		}
		tiles = backend.memory().chip.allocate(kTileCount * kPlanes * kTileSize * (kTileWidth / 16u) * 2u, 16);
		if (!tiles.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010702u);
			return;
		}
		build_tile_cache(tile_span(tiles));
		build_map(eng::Span<eng::u16>::from_raw(map_cells, kMapTilesX * kMapTilesY));

		config.map.cells = eng::Span<const eng::u16>::from_raw(map_cells, kMapTilesX * kMapTilesY);
		config.map.width = kMapTilesX;
		config.map.height = kMapTilesY;
		config.map.wrap_x = kMapTilesX;
		config.map.wrap_y = kMapTilesY;
		config.map.edge_tile = 63;
		config.tileset = static_cast<const eng::u16*>(tiles.data);
		config.tileset_count = kTileCount;
		config.tileset_planes = kPlanes;
		config.tile_width = kTileWidth; // 32px: 2 words por fila, UNA pasada
		config.tile_size = kTileSize;
		config.viewport_w = kViewportW;
		config.viewport_h = kViewportH;
		config.max_delta_x = 8;
		config.max_delta_y = 8;
		config.max_tiles_per_frame = 32;
		config.scroll_x = true;
		config.scroll_y = true;

		// Estampado inicial: la camara arranca en la posicion de la onda (0,0).
		const eng::u8 budget = 120;
		if (!field_ctrl.begin(backend.memory(), config, {0, 0})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010703u);
			return;
		}
		while (field_ctrl.busy()) {
			plan.clear();
			field_ctrl.pump(plan, budget);
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00010704u);
				return;
			}
		}

		if (!composer.init(backend.memory(), {
			palette, 1536, false, false, kPlanes,
		})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010705u);
			return;
		}

		last = wave_camera(0);
		plan.clear();
		if (!composer.compose(field_ctrl.hardware_view(0), {})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00010706u);
			return;
		}
		composer.install(backend);
		eng::debug::mark_ready(g_eng_run_status, 0x10700000u);
		ready = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) {
			return;
		}
		const ScrollPosQ16 cam = wave_camera(context.frame.frame_index);
		rest_x += cam.x - last.x;
		rest_y += cam.y - last.y;
		last = cam;
		const eng::s16 dx = static_cast<eng::s16>(rest_x / 65536);
		const eng::s16 dy = static_cast<eng::s16>(rest_y / 65536);
		rest_x -= static_cast<eng::s32>(dx) * 65536;
		rest_y -= static_cast<eng::s32>(dy) * 65536;

		plan.clear();
		plan.set_blit_budget_limits({8192, 16384, 4, 80});
		if (!field_ctrl.update(config, {dx, dy}, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00010707u);
			return;
		}
		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00010708u);
			return;
		}
		if (!composer.compose(field_ctrl.hardware_view(0), {})) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00010709u);
			return;
		}
		// Telemetría: mundo en tiles (X bits 8-15, Y bits 0-7).
		const eng::u32 marker = 0x10700000u |
			(static_cast<eng::u32>((field_ctrl.state().world_x >> 4) & 0xffu) << 8u) |
			static_cast<eng::u32>((field_ctrl.state().world_y >> 4) & 0xffu);
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
