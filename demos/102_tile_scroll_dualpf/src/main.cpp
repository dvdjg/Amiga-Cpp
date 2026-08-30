#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/field/dpf_composer.hpp>
#include <eng/field/tile_field.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/scene/route_camera.hpp>
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
namespace scene = eng::scene;

/// Modo dual 3+3: primer plano 3 planos (PF1, delante, color 0 transparente) y
/// fondo 3 planos (PF2). Total 6 bitplanes, 320x256 visible. Cada playfield
/// dispone de sus 8 colores (registros 0-7 para PF1 y 8-15 para PF2).
constexpr eng::u8 kBgPlanes = 3;
constexpr eng::u8 kFgPlanes = 3;
constexpr eng::u8 kPlanes = kBgPlanes + kFgPlanes;
constexpr eng::u8 kBackground = 1; // PF2 (planos 2,4,6): atras
constexpr eng::u8 kForeground = 0; // PF1 (planos 1,3,5): delante

constexpr eng::u16 kTileSize = 16;
constexpr eng::u16 kViewportW = 320;
constexpr eng::u16 kViewportH = 256;
constexpr eng::u16 kMapTilesX = 64;
constexpr eng::u16 kMapTilesY = 32;
constexpr eng::u8 kTileCount = 64;

/// Mapas de mundo finito (sin wrap: la cámara rebota en los bordes). Cada campo
/// tiene su propio almacenamiento y su propia semilla.
eng::u16 bg_map_cells[kMapTilesX * kMapTilesY] {};
eng::u16 fg_map_cells[kMapTilesX * kMapTilesY] {};

constexpr eng::u16 dual_palette[32] {
	// PF1 (primer plano, 3 planos): registros 0..7. Color 0 = transparente.
	0x000, 0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44, 0xfff,
	// PF2 (fondo, 3 planos): registros 8..15. Color 0 (reg 8) = transparente.
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
};

/// Hash pseudoaleatorio determinista por celda (x, y, semilla).
constexpr eng::u32 cell_hash(eng::u32 x, eng::u32 y, eng::u32 seed) {
	eng::u32 h = seed ^ (x * 0x9e3779b9u) ^ (y * 0x85ebca6bu);
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	h *= 0x846ca68bu;
	h ^= h >> 16u;
	return h;
}

/// Rellena el mapa (índices u16) con patrones derivados de una semilla.
void build_map(eng::Span<eng::u16> cells, bool is_foreground, eng::u32 seed) {
	const eng::u32 layer_seed = seed ^ (is_foreground ? 0xf0f0f0f0u : 0x0f0f0f0fu);
	for (eng::u32 y = 0; y < kMapTilesY; ++y) {
		for (eng::u32 x = 0; x < kMapTilesX; ++x) {
			const eng::u32 h = cell_hash(x, y, layer_seed);
			eng::u16 tile = static_cast<eng::u16>(h & 63u);
			if (is_foreground && ((h >> 7u) & 1u) != 0u) {
				tile = 63u; // totalmente transparente: el fondo se ve a través
			}
			cells.at(y * kMapTilesX + x) = tile;
		}
	}
}

// --- Glifos hexadecimales 5x7 y mascaras -------------------------------------

constexpr eng::u8 hex_glyph_row(eng::u8 glyph, eng::u8 row) {
	constexpr eng::u8 rows[] {
		0x0eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0eu, // 0
		0x04u, 0x0cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0eu, // 1
		0x0eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1fu, // 2
		0x1eu, 0x01u, 0x01u, 0x0eu, 0x01u, 0x01u, 0x1eu, // 3
		0x02u, 0x06u, 0x0au, 0x12u, 0x1fu, 0x02u, 0x02u, // 4
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x01u, 0x01u, 0x1eu, // 5
		0x0eu, 0x10u, 0x10u, 0x1eu, 0x11u, 0x11u, 0x0eu, // 6
		0x1fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u, // 7
		0x0eu, 0x11u, 0x11u, 0x0eu, 0x11u, 0x11u, 0x0eu, // 8
		0x0eu, 0x11u, 0x11u, 0x0fu, 0x01u, 0x01u, 0x0eu, // 9
		0x0eu, 0x11u, 0x11u, 0x1fu, 0x11u, 0x11u, 0x11u, // A
		0x1eu, 0x11u, 0x11u, 0x1eu, 0x11u, 0x11u, 0x1eu, // B
		0x0eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0eu, // C
		0x1eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1eu, // D
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x1fu, // E
		0x1fu, 0x10u, 0x10u, 0x1eu, 0x10u, 0x10u, 0x10u, // F
	};
	return rows[static_cast<eng::u16>(glyph & 0x0fu) * 7u + (row % 7u)];
}

constexpr eng::u16 row_mask_range(eng::u8 left, eng::u8 width) {
	eng::u16 mask = 0;
	for (eng::u8 i = 0; i < width; ++i) {
		mask |= static_cast<eng::u16>(0x8000u >> (left + i));
	}
	return mask;
}

constexpr eng::u16 glyph_row_mask(eng::u8 glyph, eng::u8 y) {
	if (y < 1u || y >= 15u) {
		return 0;
	}
	const eng::u8 glyph_y = static_cast<eng::u8>((y - 1u) / 2u);
	const eng::u8 bits = hex_glyph_row(glyph, glyph_y);
	eng::u16 mask = 0;
	for (eng::u8 glyph_x = 0; glyph_x < 5u; ++glyph_x) {
		if ((bits & (1u << (4u - glyph_x))) != 0u) {
			mask |= row_mask_range(static_cast<eng::u8>(3u + glyph_x * 2u), 2);
		}
	}
	return mask;
}

constexpr eng::u16 variant_marker_mask(eng::u8 variant, eng::u8 y) {
	const eng::u8 marker_size = static_cast<eng::u8>(2u + (variant & 1u));
	eng::u16 mask = 0;
	if (y < marker_size) {
		mask |= row_mask_range(1, marker_size);
	}
	if (variant >= 2u && y >= static_cast<eng::u8>(15u - marker_size)) {
		mask |= row_mask_range(static_cast<eng::u8>(15u - marker_size), marker_size);
	}
	return mask;
}

// --- Tiles de 3 planos por playfield (dual) ----------------------------------

/// Compone una fila planar de un tile simbólico de 3 planos para un playfield.
constexpr eng::u16 pf_plane_row(
	eng::u8 glyph,
	eng::u8 variant,
	eng::u8 y,
	eng::u8 plane,
	eng::u8 base,
	bool transparent_bg
) {
	constexpr eng::u8 bg_colors[] {1, 2, 3, 4};
	constexpr eng::u8 ink_colors[] {7, 6, 5, 7};
	constexpr eng::u8 border_colors[] {5, 3, 1, 6};
	const eng::u8 bg = static_cast<eng::u8>(base + bg_colors[variant & 3u]);
	const eng::u8 ink = static_cast<eng::u8>(base + ink_colors[variant & 3u]);
	const eng::u8 border = static_cast<eng::u8>(base + border_colors[variant & 3u]);

	const eng::u16 border_mask = (y == 0u || y == 15u) ? 0xffffu : 0x8001u;
	const eng::u16 marker_mask = static_cast<eng::u16>(variant_marker_mask(variant, y) & ~border_mask);
	const eng::u16 glyph_mask = static_cast<eng::u16>(glyph_row_mask(glyph, y) & ~(border_mask | marker_mask));
	const eng::u16 bg_mask = static_cast<eng::u16>(~(border_mask | marker_mask | glyph_mask));

	eng::u16 row = 0;
	if (transparent_bg) {
		const eng::u16 checker = (y & 1u) == 0u ? 0xaaaa : 0x5555;
		if ((bg & (1u << plane)) != 0u) {
			row |= static_cast<eng::u16>(bg_mask & checker);
		}
	} else if ((bg & (1u << plane)) != 0u) {
		row |= bg_mask;
	}
	if ((border & (1u << plane)) != 0u) {
		row |= border_mask;
	}
	if ((ink & (1u << plane)) != 0u) {
		row |= static_cast<eng::u16>(marker_mask | glyph_mask);
	}
	return row;
}

constexpr eng::u16 bg_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	return pf_plane_row(glyph, variant, y, plane, 8, false);
}

constexpr eng::u16 fg_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	// Tile 63 (glifo F, variante 3): COMPLETAMENTE transparente, todas sus filas
	// a 0 en los 3 planos -> color 0 del playfield (transparente).
	if (glyph == 15u && variant == 3u) {
		return 0;
	}
	return pf_plane_row(glyph, variant, y, plane, 0, true);
}

/// Construye el tileset del campo (planar contiguo por playfield).
void build_tile_cache(eng::Span<eng::u16> words, eng::u8 tile_planes, bool is_foreground) {
	const eng::u32 words_per_tile = words.size() / kTileCount;
	for (eng::u16 tile = 0; tile < kTileCount; ++tile) {
		const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
		const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
		const bool fully_transparent = is_foreground && tile == 63u;
		for (eng::u8 y = 0; y < kTileSize; ++y) {
			for (eng::u8 plane = 0; plane < tile_planes; ++plane) {
				words.at(static_cast<eng::u32>(tile) * words_per_tile + static_cast<eng::u32>(plane) * kTileSize + y) =
					fully_transparent
						? 0
						: (is_foreground
							? fg_plane_row(glyph, variant, y, plane)
							: bg_plane_row(glyph, variant, y, plane));
			}
		}
	}
}

/// Seno de 64 pasos exactos (amplitud 64).
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

/// Seno interpolado con precisión Q16 (escala 65536 = 1.0) para avance
/// sub-pixel suave: conserva la fracción en vez de redondear a enteros.
constexpr eng::s32 sin_smooth(eng::u32 frame_index, eng::u32 period) {
	const eng::u32 ph = (frame_index * 4096u) / period;
	const eng::u8 i = static_cast<eng::u8>((ph >> 6) & 63u);
	const eng::u8 frac = static_cast<eng::u8>(ph & 63u);
	const eng::s32 s0 = sin64(i);
	const eng::s32 s1 = sin64(static_cast<eng::u8>(i + 1u) & 63u);
	return (s0 * static_cast<eng::s32>(64 - frac) + s1 * static_cast<eng::s32>(frac)) * 1024;
}

/// Camara del primer plano: onda de Lissajous en sub-píxeles (Q16).
///
/// Rango contenido dentro del mundo FINITO de la 102 (64x32 tiles = 1024x512):
/// la cámara recorre ~64..256 en X y ~32..224 en Y, sin tocar los bordes (el
/// max scroll Y del mundo finito es 256). En la migracion inicial se uso el
/// Lissajous grande de la 106 (0..640), que excedia el mundo y la camara se
/// clavaba en el borde -> salto visible.
struct CameraQ16 {
	eng::s32 x = 0; // px * 65536
	eng::s32 y = 0;
};

constexpr CameraQ16 fg_wave_camera(eng::u32 frame_index) {
	const eng::s32 sx = sin_smooth(frame_index, 640) * 96 / 64;
	const eng::s32 sy = sin_smooth(frame_index, 512) * 96 / 64;
	return {
		(160 + sx) * 65536,
		(128 + sy) * 65536,
	};
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
	scene::RouteCamera m_bg_cam {};
	eng::u32 tiles_uploaded = 0;
	bool ready = false;

	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({280u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000210u);
			return;
		}

		tiles_bg = backend.memory().chip.allocate(kTileCount * kBgPlanes * kTileSize * 2u, 16);
		tiles_fg = backend.memory().chip.allocate(kTileCount * kFgPlanes * kTileSize * 2u, 16);
		if (!tiles_bg.valid() || !tiles_fg.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000211u);
			return;
		}
		build_tile_cache(tile_span(tiles_bg), kBgPlanes, false);
		build_tile_cache(tile_span(tiles_fg), kFgPlanes, true);

		build_map(eng::Span<eng::u16>::from_raw(bg_map_cells, kMapTilesX * kMapTilesY), false, 0x13579bdu);
		build_map(eng::Span<eng::u16>::from_raw(fg_map_cells, kMapTilesX * kMapTilesY), true, 0x2468aceu);

		bg_config = make_config(tiles_bg, kBgPlanes, false);
		fg_config = make_config(tiles_fg, kFgPlanes, true);

		// Estampado inicial por Blitter, en lotes hasta terminar.
		// Las camaras arrancan en su posicion inicial para que el primer update no
		// tenga un salto (el Lissajous empieza en (160,128) y la ruta en x=1).
		const eng::s32 bg_x0 = 1;
		const eng::s32 bg_y0 = m_bg_cam.center_y;
		const eng::s32 fg_x0 = 160;
		const eng::s32 fg_y0 = 128;
		const eng::u8 budget = 120;
		if (!bg.begin(backend.memory(), bg_config, {static_cast<eng::s16>(bg_x0), static_cast<eng::s16>(bg_y0)})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000212u);
			return;
		}
		while (bg.busy()) {
			plan.clear();
			bg.pump(plan, budget);
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000213u);
				return;
			}
		}
		plan.clear();
		if (!fg.begin(backend.memory(), fg_config, {static_cast<eng::s16>(fg_x0), static_cast<eng::s16>(fg_y0)})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000214u);
			return;
		}
		while (fg.busy()) {
			plan.clear();
			fg.pump(plan, budget);
			if (!backend.execute_frame_plan(plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000215u);
				return;
			}
		}

		if (!composer.init(backend.memory(), {
			dual_palette, 1536, true, false, kPlanes,
		})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000216u);
			return;
		}

		// Camaras en la posicion inicial de la primera fase de la ruta.
		m_bg_cam.x = 1;
		m_bg_cam.y = m_bg_cam.center_y;
		m_fg_cam = fg_wave_camera(0);
		m_bg_last_x = m_bg_cam.x;
		m_bg_last_y = m_bg_cam.y;
		m_fg_last_x = m_fg_cam.x;
		m_fg_last_y = m_fg_cam.y;

		plan.clear();
		if (!composer.compose(fg.hardware_view(kForeground), bg.hardware_view(kBackground))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000217u);
			return;
		}
		composer.install(backend);

		publish_status();
		eng::debug::DebugPeripheral::counter_name(0, reinterpret_cast<eng::u32>("tiles_uploaded"));
		ready = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!ready) {
			return;
		}

		const eng::u32 frame = context.frame.frame_index;

		// Fondo: RouteCamera por fases (parallax). Primer plano: Lissajous propia.
		m_bg_cam.advance(frame);
		m_fg_cam = fg_wave_camera(frame);

		plan.clear();
		plan.set_blit_budget_limits({4096, 8192, 4, 80});

		const field::TileScrollOffset bg_delta {
			static_cast<eng::s16>(m_bg_cam.x - m_bg_last_x),
			static_cast<eng::s16>(m_bg_cam.y - m_bg_last_y),
		};
		m_bg_last_x = m_bg_cam.x;
		m_bg_last_y = m_bg_cam.y;

		fg_rest_x += m_fg_cam.x - m_fg_last_x;
		fg_rest_y += m_fg_cam.y - m_fg_last_y;
		m_fg_last_x = m_fg_cam.x;
		m_fg_last_y = m_fg_cam.y;
		const eng::s16 fg_dx = static_cast<eng::s16>(fg_rest_x / 65536);
		const eng::s16 fg_dy = static_cast<eng::s16>(fg_rest_y / 65536);
		fg_rest_x -= static_cast<eng::s32>(fg_dx) * 65536;
		fg_rest_y -= static_cast<eng::s32>(fg_dy) * 65536;
		const field::TileScrollOffset fg_delta {fg_dx, fg_dy};

		if (!bg.update(bg_config, bg_delta, plan) || !fg.update(fg_config, fg_delta, plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00000218u);
			return;
		}
		tiles_uploaded += plan.blit_budget().tile_jobs;
		eng::debug::DebugPeripheral::counter_value(0, tiles_uploaded);

		if (!backend.execute_frame_plan(plan)) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x00000219u);
			return;
		}

		if (!composer.compose(fg.hardware_view(kForeground), bg.hardware_view(kBackground))) {
			ready = false;
			eng::debug::mark_failed(g_eng_run_status, 0x0000021au);
			return;
		}
		// Telemetría: latch de movimiento por eje de cada campo (flags). En un
		// mundo finito que rebota la cámara no siempre cruza una página completa;
		// lo que importa es que cada campo scrollee en X e Y (parallax).
		m_moved_bg_x |= bg_delta.x != 0;
		m_moved_bg_y |= bg_delta.y != 0;
		m_moved_fg_x |= fg_delta.x != 0;
		m_moved_fg_y |= fg_delta.y != 0;
		publish_status();
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

	field::TileFieldConfig make_config(const eng::MemoryBlock& tiles, eng::u8 planes, bool is_foreground) {
		field::TileFieldConfig config {};
		config.map.cells = is_foreground
			? eng::Span<const eng::u16>::from_raw(fg_map_cells, kMapTilesX * kMapTilesY)
			: eng::Span<const eng::u16>::from_raw(bg_map_cells, kMapTilesX * kMapTilesY);
		config.map.width = kMapTilesX;
		config.map.height = kMapTilesY;
		config.map.wrap_x = 0; // mundo finito: la camara rebota en los bordes
		config.map.wrap_y = 0;
		config.map.edge_tile = 63;
		config.tileset = static_cast<const eng::u16*>(tiles.data);
		config.tileset_count = kTileCount;
		config.tileset_planes = planes;
		config.tile_width = 16;
		config.tile_size = kTileSize;
		config.viewport_w = kViewportW;
		config.viewport_h = kViewportH;
		// El movimiento real del fg es ~1.5px/frame y el de la ruta del fondo
		// tambien pequeno; margen amplio sin recortar trayectorias.
		config.max_delta_x = 5;
		config.max_delta_y = 5;
		config.max_tiles_per_frame = 32;
		config.scroll_x = true;
		config.scroll_y = true;
		return config;
	}

	void publish_status() {
		const eng::u8 bg_x = static_cast<eng::u8>(m_bg_cam.x & 0xffu);
		const eng::u8 fg_x = static_cast<eng::u8>((m_fg_cam.x >> 16) & 0xffu);
		eng::debug::mark_ready(
			g_eng_run_status,
			0x12000000u |
				(static_cast<eng::u32>(bg_x) << 16u) |
				(static_cast<eng::u32>(fg_x) << 8u) |
				(static_cast<eng::u32>(m_moved_bg_x & 1u) << 0u) |
				(static_cast<eng::u32>(m_moved_bg_y & 1u) << 1u) |
				(static_cast<eng::u32>(m_moved_fg_x & 1u) << 2u) |
				(static_cast<eng::u32>(m_moved_fg_y & 1u) << 3u)
		);
	}

	CameraQ16 m_fg_cam {};
	eng::s32 m_bg_last_x = 0;
	eng::s32 m_bg_last_y = 0;
	eng::s32 m_fg_last_x = 0;
	eng::s32 m_fg_last_y = 0;
	eng::s32 fg_rest_x = 0;
	eng::s32 fg_rest_y = 0;
	eng::u8 m_moved_bg_x = 0;
	eng::u8 m_moved_bg_y = 0;
	eng::u8 m_moved_fg_x = 0;
	eng::u8 m_moved_fg_y = 0;
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
