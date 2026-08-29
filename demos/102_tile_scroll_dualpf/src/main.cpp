#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/debug/peripheral.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/tilemap/tile_scroll.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/scene/route_camera.hpp>
#include <eng/scene/virtual_scene.hpp>
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

namespace drivers = eng::graphics::drivers;
namespace scene = eng::scene;
namespace tilemap = eng::graphics::tilemap;

/// Modo dual 3+3: primer plano 3 planos (PF1, delante, color 0 transparente) y
/// fondo 3 planos (PF2). Total 6 bitplanes, 320x256 visible. Cada playfield
/// dispone de sus 8 colores (registros 0-7 para PF1 y 8-15 para PF2).
constexpr auto kMode = drivers::TileScrollMode::dual(3, 3);
using Scene = drivers::TileScrollScene<kMode>;
using Ring = drivers::BidirectionalRingPrefetch<
	Scene::visible_width / Scene::tile_size,
	Scene::visible_height / Scene::tile_size,
	Scene::surface_width / Scene::tile_size,
	Scene::surface_height / Scene::tile_size
>;

constexpr eng::u16 tile_size = Scene::tile_size;
constexpr eng::u16 map_tiles_x = 64;
constexpr eng::u16 map_tiles_y = 32;
constexpr eng::u16 surface_tiles_x = Scene::surface_width / tile_size;
constexpr eng::u16 surface_tiles_y = Scene::surface_height / tile_size;
constexpr eng::u8 bg_pattern_count = 64;
constexpr eng::u8 fg_pattern_count = 64;
constexpr eng::u8 tile_update_budget = 1;

/// Fondo PF2 (3 planos): playfield 1 de la escena, colores 8..15.
constexpr eng::u8 pf_background = 1;
/// Primer plano PF1 (3 planos): playfield 0 de la escena, colores 0..7.
/// PF1 queda delante (BPL2PRI=0) y su color 0 es transparente.
constexpr eng::u8 pf_foreground = 0;

constexpr drivers::EhbPalette dual_palette {{
	// PF1 (primer plano, 3 planos): registros 0..7. Color 0 = transparente.
	0x000, 0xf0c, 0x0cf, 0xff0, 0xf80, 0x84f, 0xf44, 0xfff,
	// PF2 (fondo, 3 planos): registros 8..15. Color 0 (reg 8) = transparente.
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xfff,
}};

constexpr drivers::EhbPaletteZone palette_zones[] {};

// --- Glifos hexadecimales 5x7 y mascaras (igual que la demo 101) -------------

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

/// Mascara de los marcadores de variante (esquinas que distinguen patrones).
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

/// Compone una fila planar de un tile simbolico de 3 planos para un playfield.
///
/// `base` es el indice del primer color del playfield (0 para PF1, 8 para PF2),
/// asi todos los colores quedan dentro del banco de ese playfield y el color 0
/// del playfield (`base + 0`) es el transparente del modo dual. Cada tile
/// combina cuatro ideas:
///
/// - fondo (borde de color tramado al 50% si `transparent_bg`, opaco si no);
/// - borde de alto contraste en los bordes del tile;
/// - glifo hexadecimal grande;
/// - marcador de variante en las esquinas.
///
/// Los colores cambian por variante para que la capa muestre todo su rango.
constexpr eng::u16 pf_plane_row(
	eng::u8 glyph,
	eng::u8 variant,
	eng::u8 y,
	eng::u8 plane,
	eng::u8 base,
	bool transparent_bg
) {
	// Tablas por variante que cubren TODOS los indices opacos del banco
	// (1..7 para PF1, 9..15 para PF2) para que cada capa muestre todo su rango.
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
		// Fondo tramado al 50%: el color del fondo ocupa la mitad de los pixels y
		// la otra mitad queda en el color 0 del playfield (transparente), asi el
		// playfield de atras se ve entre medias.
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

/// Tiles del fondo (PF2, 3 planos, colores 8..15): fondo opaco.
constexpr eng::u16 bg_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	return pf_plane_row(glyph, variant, y, plane, 8, false);
}

/// Tiles del primer plano (PF1, 3 planos, colores 0..7): fondo tramado
/// transparente para que el fondo se vea a traves.
constexpr eng::u16 fg_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	return pf_plane_row(glyph, variant, y, plane, 0, true);
}

// --- Caches y mapas ----------------------------------------------------------

eng::Span<const eng::u16> tile_source(
	const eng::MemoryBlock& block,
	eng::u16 tile_index,
	eng::u16 pattern_count,
	eng::u8 planes
) {
	const eng::u32 words_per_tile = (Scene::tile_plane_bytes() / sizeof(eng::u16)) * planes;
	const eng::u32 word_offset = static_cast<eng::u32>(tile_index & (pattern_count - 1u)) * words_per_tile;
	return {
		reinterpret_cast<const eng::u16*>(static_cast<const eng::u8*>(block.data)) + word_offset,
		words_per_tile,
	};
}

void build_tile_cache(eng::Span<eng::u16> words, eng::u16 pattern_count, eng::u8 planes, bool is_foreground) {
	const eng::u32 words_per_tile = words.size() / pattern_count;
	for (eng::u16 tile = 0; tile < pattern_count; ++tile) {
		const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
		const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
		for (eng::u8 y = 0; y < tile_size; ++y) {
			for (eng::u8 plane = 0; plane < planes; ++plane) {
			words.at(static_cast<eng::u32>(tile) * words_per_tile + static_cast<eng::u32>(plane) * tile_size + y) =
				is_foreground
					? fg_plane_row(glyph, variant, y, plane)
					: bg_plane_row(glyph, variant, y, plane);
			}
		}
	}
}

/// Hash pseudoaleatorio determinista por celda (x, y, semilla).
constexpr eng::u32 cell_hash(eng::u16 x, eng::u16 y, eng::u32 seed) {
	eng::u32 h = seed ^ (static_cast<eng::u32>(x) * 0x9e3779b9u) ^ (static_cast<eng::u32>(y) * 0x85ebca6bu);
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	h *= 0x846ca68bu;
	h ^= h >> 16u;
	return h;
}

/// Rellena un mapa con patrones pseudoaleatorios derivados de una semilla.
///
/// Ambos playfields usan glifo (bajo) + variante (alto). La variante decide los
/// colores del tile, asi cada capa recorre todos los colores de su banco de
/// paleta. Cambiar la semilla regenera el patron completo; la demo la cambia
/// periodicamente para que el mundo "mute" de aspecto.
void build_map(tilemap::PackedTileCell* cells, bool is_foreground, eng::u32 seed) {
	const eng::u32 layer_seed = seed ^ (is_foreground ? 0xf0f0f0f0u : 0x0f0f0f0fu);
	for (eng::u16 y = 0; y < map_tiles_y; ++y) {
		for (eng::u16 x = 0; x < map_tiles_x; ++x) {
			const eng::u32 h = cell_hash(x, y, layer_seed);
			const eng::u16 symbol = static_cast<eng::u16>(h & 0x0fu);
			const eng::u16 variant = static_cast<eng::u16>((h >> 4u) & 3u);
			cells[static_cast<eng::u32>(y) * map_tiles_x + x].set_tile(
				static_cast<eng::u16>(symbol | (variant << 4u))
			);
		}
	}
}

// --- Capa por playfield -------------------------------------------------------

/// Franja de prefetch en curso de una capa.
struct PendingStrip {
	eng::u16 world_index = Ring::unknown_index;
	eng::u16 slot = 0;
	eng::u8 remaining = 0;
	bool is_column = false;

	constexpr bool active() const {
		return remaining != 0;
	}
};

/// Una capa (fondo o primer plano): mapa, scheduler, anillo y cache propios.
struct Layer {
	tilemap::PackedTileCell cells[map_tiles_x * map_tiles_y] {};
	tilemap::TileMap16 map {};
	tilemap::ProgressiveTileScheduler scheduler {};
	Ring ring {};
	eng::MemoryBlock tiles {};
	eng::u8 pattern_count = 0;
	eng::u8 planes = 0;
	eng::u16 previous_tile_x = 0;
	eng::u16 previous_tile_y = 0;
	eng::u8 recycled_columns = 0;
	eng::u8 recycled_rows = 0;
	PendingStrip pending[4] {};
};

constexpr eng::u16 visible_safe_right_column(eng::u16 camera_tile_x) {
	return static_cast<eng::u16>(camera_tile_x + Ring::visible_columns + 1u);
}

constexpr eng::u16 visible_safe_bottom_row(eng::u16 camera_tile_y) {
	return static_cast<eng::u16>(camera_tile_y + Ring::visible_rows + 1u);
}

void layer_enqueue(Layer& layer, tilemap::TileRect rect, tilemap::TileUpdateEdge edge) {
	if (rect.left >= surface_tiles_x || rect.top >= surface_tiles_y) {
		return;
	}

	const bool is_column = edge == tilemap::TileUpdateEdge::Left || edge == tilemap::TileUpdateEdge::Right;
	const eng::u16 world_index = is_column ? rect.left : rect.top;

	PendingStrip* free_slot = nullptr;
	for (PendingStrip& pending : layer.pending) {
		if (pending.active()) {
			if (pending.is_column == is_column && pending.world_index == world_index) {
				return;
			}
		} else if (free_slot == nullptr) {
			free_slot = &pending;
		}
	}
	if (free_slot == nullptr) {
		return;
	}

	const eng::u8 enqueued = layer.scheduler.enqueue_strip(layer.map, rect, edge, 0, 12, 1);
	if (enqueued == 0) {
		return;
	}
	*free_slot = PendingStrip {
		world_index,
		is_column ? layer.ring.slot_for_world_column(world_index) : layer.ring.slot_for_world_row(world_index),
		enqueued,
		is_column,
	};
}

void layer_schedule(Layer& layer, eng::u16 camera_world_column, eng::u16 camera_world_row) {
	const eng::u16 previous_column = layer.previous_tile_x;
	const eng::u16 previous_row = layer.previous_tile_y;
	layer.previous_tile_x = camera_world_column;
	layer.previous_tile_y = camera_world_row;
	if (previous_column == camera_world_column && previous_row == camera_world_row) {
		return;
	}

	if (camera_world_column > previous_column) {
		for (eng::u16 column = static_cast<eng::u16>(previous_column + visible_safe_right_column(0) + 1u);
			 column <= visible_safe_right_column(camera_world_column);
			 ++column) {
			layer_enqueue(layer, {column, camera_world_row, 1, Ring::visible_rows}, tilemap::TileUpdateEdge::Right);
		}
	} else if (camera_world_column < previous_column && camera_world_column != 0) {
		layer_enqueue(layer, {static_cast<eng::u16>(camera_world_column - 1u), camera_world_row, 1, Ring::visible_rows}, tilemap::TileUpdateEdge::Left);
	}

	if (camera_world_row > previous_row) {
		for (eng::u16 row = static_cast<eng::u16>(previous_row + visible_safe_bottom_row(0) + 1u);
			 row <= visible_safe_bottom_row(camera_world_row);
			 ++row) {
			layer_enqueue(layer, {camera_world_column, row, Ring::visible_columns, 1}, tilemap::TileUpdateEdge::Bottom);
		}
	} else if (camera_world_row < previous_row && camera_world_row != 0) {
		layer_enqueue(layer, {camera_world_column, static_cast<eng::u16>(camera_world_row - 1u), Ring::visible_columns, 1}, tilemap::TileUpdateEdge::Top);
	}
}

/// Consume presupuesto de tiles de una capa y devuelve el numero de blits anadidos.
eng::u8 layer_upload(Layer& layer, Scene& scene, eng::graphics::FramePlan& plan, eng::u8 playfield) {
	const tilemap::ProgressiveTileUpdatePlan jobs_plan = layer.scheduler.take_budget(tile_update_budget);
	eng::u8 blit_jobs = 0;
	for (eng::u8 i = 0; i < jobs_plan.count; ++i) {
		const tilemap::TileUpdateJob& job = jobs_plan.jobs[i];
		const eng::u16 surface_col = static_cast<eng::u16>(job.x % Ring::surface_columns);
		const eng::u16 surface_row = static_cast<eng::u16>(job.y % Ring::surface_rows);
		eng::graphics::BlitJob blits[3] {};
		const eng::u8 blit_count = scene.make_playfield_upload_jobs(
			playfield,
			tile_source(layer.tiles, job.tile_index, layer.pattern_count, layer.planes).data(),
			surface_col,
			surface_row,
			blits
		);
		for (eng::u8 b = 0; b < blit_count; ++b) {
			plan.add_tile_block_copy(blits[b]);
		}
		blit_jobs = static_cast<eng::u8>(blit_jobs + blit_count);
	}

	for (eng::u8 i = 0; i < jobs_plan.count; ++i) {
		const tilemap::TileUpdateJob& job = jobs_plan.jobs[i];
		const bool is_column = job.edge == tilemap::TileUpdateEdge::Left || job.edge == tilemap::TileUpdateEdge::Right;
		const eng::u16 world_index = is_column ? job.x : job.y;
		for (PendingStrip& pending : layer.pending) {
			if (!pending.active() || pending.is_column != is_column || pending.world_index != world_index) {
				continue;
			}
			--pending.remaining;
			if (pending.remaining == 0) {
				if (pending.is_column) {
					layer.ring.mark_column_ready(pending.slot, pending.world_index);
					++layer.recycled_columns;
				} else {
					layer.ring.mark_row_ready(pending.slot, pending.world_index);
					++layer.recycled_rows;
				}
				pending.world_index = Ring::unknown_index;
				pending.remaining = 0;
			}
			break;
		}
	}
	return blit_jobs;
}

void stamp_layer(Scene& scene, const Layer& layer, eng::u8 playfield) {
	for (eng::u16 y = 0; y < surface_tiles_y; ++y) {
		for (eng::u16 x = 0; x < surface_tiles_x; ++x) {
			const eng::u16 tile = layer.cells[static_cast<eng::u32>(y) * map_tiles_x + x].tile_index();
			const eng::Span<const eng::u16> src = tile_source(layer.tiles, tile, layer.pattern_count, layer.planes);
			for (eng::u8 plane_in_pf = 0; plane_in_pf < layer.planes; ++plane_in_pf) {
				const eng::u8 hw_plane = Scene::hardware_plane_of(playfield, plane_in_pf);
				for (eng::u16 row = 0; row < tile_size; ++row) {
					const eng::u32 index =
						static_cast<eng::u32>(y * tile_size + row) * (Scene::surface_bytes_per_row / sizeof(eng::u16)) +
						x;
					scene.plane_words(hw_plane).at(index) =
						src.at(static_cast<eng::u32>(plane_in_pf) * tile_size + row);
				}
			}
		}
	}
}

void configure_tile_blit_budget(eng::graphics::FramePlan& plan) {
	// Ambos playfields tienen 3 planos: un tile = 3 jobs x 16 words. Con un tile
	// por capa por frame: 6 jobs / 96 words.
	plan.set_blit_budget_limits({
		64,
		128,
		4,
		8,
	});
}

// --- Camaras de ruta por fases (parallax) -------------------------------------
//
// El fondo recorre las fases (horizontal, vertical, diagonal, circular y
// senoidal) con `RouteCamera`. El primer plano usa la misma ruta pero con el
// espejo horizontal activado, de modo que mientras uno deriva a la derecha el
// otro lo hace a la izquierda (parallax opuesto) sin tocar el mapa.

/// Seno de 64 pasos (amplitud 64) para el movimiento ondulado del primer plano.
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

/// Seno interpolado con avance sub-pixel para animacion suave.
///
/// La tabla `sin64` tiene 64 pasos por ciclo y cada paso mueve hasta ~6 px
/// (con radio 96, hasta ~9 px). Si el indice avanzase 1 paso por frame, el
/// movimiento saltaria ~9 px: a trompicones. Aqui la fase avanza en fracciones
/// (0..256 por ciclo, interpolacion lineal entre pasos adyacentes) de modo que
/// el desplazamiento por frame sea ~1 px. Devuelve un valor en [-64, 64].
constexpr eng::s16 sin_smooth(eng::u32 frame_index, eng::u32 period) {
	// fase en unidades 0..256 por ciclo; cada paso de la tabla de 64 cubre 4
	// unidades, asi que el indice es (fase >> 2) y el resto es la fraccion.
	const eng::u32 ph = (frame_index * 256u) / period;
	const eng::u8 i = static_cast<eng::u8>((ph >> 2) & 63u);
	const eng::u8 frac = static_cast<eng::u8>(ph & 3u);
	const eng::s16 s0 = sin64(i);
	const eng::s16 s1 = sin64(static_cast<eng::u8>(i + 1u) & 63u);
	return static_cast<eng::s16>((s0 * static_cast<eng::s16>(4 - frac) + s1 * static_cast<eng::s16>(frac)) / 4);
}

/// Camara del primer plano: onda de Lissajous independiente del fondo.
///
/// Periodos largos (640 y 512 frames) y seno interpolado => desplazamiento
/// ~1 px/frame suave, con pocos cruces de tile (el prefetch no se satura y la
/// demo se mantiene a 50 fps).
constexpr drivers::ScrollPosition2 fg_wave_camera(eng::u32 frame_index) {
	const eng::u16 x = static_cast<eng::u16>(160 + sin_smooth(frame_index, 640) * 96 / 64);
	const eng::u16 y = static_cast<eng::u16>(128 + sin_smooth(frame_index, 512) * 96 / 64);
	return {x, y};
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({280u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000210u);
			return;
		}

		const drivers::TileScrollConfig config {
			&dual_palette,
			palette_zones,
			0,
			1536,
		};
		if (!m_scene.init(backend.memory(), config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000211u);
			return;
		}

		init_layer(backend, m_background, pf_background, bg_pattern_count);
		init_layer(backend, m_foreground, pf_foreground, fg_pattern_count);
		if (!m_background.tiles.valid() || !m_foreground.tiles.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000212u);
			return;
		}

		m_scene.bitplane_span().clear();
		build_tile_cache(tile_span(m_background.tiles), bg_pattern_count, m_background.planes, false);
		build_tile_cache(tile_span(m_foreground.tiles), fg_pattern_count, m_foreground.planes, true);
		// Mapa inicial con semilla fija (el mundo NO cambia durante la demo).
		build_map(m_background.cells, false, 0x13579bdu);
		build_map(m_foreground.cells, true, 0x2468aceu);
		m_background.map.reset(m_background.cells, map_tiles_x, map_tiles_y);
		m_foreground.map.reset(m_foreground.cells, map_tiles_x, map_tiles_y);
		m_fg_cam.mirror_x = true;

		stamp_layer(m_scene, m_background, pf_background);
		stamp_layer(m_scene, m_foreground, pf_foreground);

		m_background.scheduler.reset();
		m_foreground.scheduler.reset();
		m_background.ring.reset(0, 0);
		m_foreground.ring.reset(0, 0);

		// Camaras en la posicion inicial de la primera fase de la ruta.
		m_bg_cam.x = 1;
		m_bg_cam.y = m_bg_cam.center_y;
		m_fg_cam.x = 1;
		m_fg_cam.y = m_fg_cam.center_y;
		const drivers::ScrollPosition2 bg {m_bg_cam.x, m_bg_cam.y};
		const drivers::ScrollPosition2 fg {m_fg_cam.x, m_fg_cam.y};
		m_background.previous_tile_x = bg.x / tile_size;
		m_background.previous_tile_y = bg.y / tile_size;
		m_foreground.previous_tile_x = fg.x / tile_size;
		m_foreground.previous_tile_y = fg.y / tile_size;

		drivers::TileScrollInput input {};
		input.playfield[pf_background] = bg;
		input.playfield[pf_foreground] = fg;
		if (!m_scene.rebuild_copper(input)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000214u);
			return;
		}

		m_scene.install(backend);
		publish_status(bg, fg, 0);
		eng::debug::DebugPeripheral::counter_name(0, reinterpret_cast<eng::u32>("tiles_uploaded"));
		m_ready = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}

		const eng::u32 frame = context.frame.frame_index;

		m_bg_cam.advance(frame);
		// El primer plano hace SIEMPRE un movimiento senoidal propio (onda de
		// Lissajous), distinto de la fase que este recorriendo el fondo: si el
		// fondo va lateral, el primer plano va oblicuo/ondulado.
		const drivers::ScrollPosition2 bg {m_bg_cam.x, m_bg_cam.y};
		const drivers::ScrollPosition2 fg {fg_wave_camera(frame)};

		layer_schedule(m_background, bg.x / tile_size, bg.y / tile_size);
		layer_schedule(m_foreground, fg.x / tile_size, fg.y / tile_size);

		m_frame_plan.clear();
		configure_tile_blit_budget(m_frame_plan);
		const eng::u8 bg_jobs = layer_upload(m_background, m_scene, m_frame_plan, pf_background);
		const eng::u8 fg_jobs = layer_upload(m_foreground, m_scene, m_frame_plan, pf_foreground);
		m_total_tiles_uploaded += static_cast<eng::u32>(bg_jobs) + fg_jobs;
		eng::debug::DebugPeripheral::counter_value(0, m_total_tiles_uploaded);
		if (m_frame_plan.blit_budget_report().status == eng::graphics::BlitBudgetStatus::Exceeded) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000217u);
			return;
		}
		if (!backend.execute_frame_plan(m_frame_plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000216u);
			return;
		}

		drivers::TileScrollInput input {};
		input.playfield[pf_background] = bg;
		input.playfield[pf_foreground] = fg;
		if (!m_scene.rebuild_copper(input)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000215u);
			return;
		}
		publish_status(bg, fg, static_cast<eng::u8>(bg_jobs + fg_jobs));
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			m_scene.install(backend);
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

	void init_layer(eng::amiga::MinimalBackend& backend, Layer& layer, eng::u8 playfield, eng::u8 pattern_count) {
		layer.pattern_count = pattern_count;
		layer.planes = Scene::playfield_planes(playfield);
		layer.tiles = backend.memory().chip.allocate(
			Scene::playfield_tile_bytes(playfield) * pattern_count,
			16
		);
	}

	void publish_status(drivers::ScrollPosition2 bg, drivers::ScrollPosition2 fg, eng::u8 tile_jobs) {
		const eng::u8 flags = static_cast<eng::u8>(
			(m_background.recycled_columns != 0 || m_background.recycled_rows != 0 ? 0x1u : 0u) |
			(m_foreground.recycled_columns != 0 || m_foreground.recycled_rows != 0 ? 0x2u : 0u)
		);
		eng::debug::mark_ready(
			g_eng_run_status,
			0x12000000u |
				(static_cast<eng::u32>(bg.x & 0xffu) << 16u) |
				(static_cast<eng::u32>(fg.x & 0xffu) << 8u) |
				(static_cast<eng::u32>(tile_jobs & 0x0fu) << 4u) |
				static_cast<eng::u32>(flags)
		);
	}

	bool m_ready = false;
	Scene m_scene {};
	Layer m_background {};
	Layer m_foreground {};
	eng::graphics::FramePlan m_frame_plan {};
	scene::RouteCamera m_bg_cam {};
	scene::RouteCamera m_fg_cam {};
	eng::u32 m_total_tiles_uploaded = 0;
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
