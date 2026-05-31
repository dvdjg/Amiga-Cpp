#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/drivers/ehb_tile_scroll.hpp>
#include <amg/graphics/frame_plan.hpp>
#include <amg/graphics/tilemap/tile_scroll.hpp>
#include <amg/platform/amiga_minimal.hpp>
#include <amg/scene/virtual_scene.hpp>

#include <proto/exec.h>
#include <exec/execbase.h>

#include "support/gcc8_c_support.h"

struct ExecBase* SysBase = nullptr;

extern "C" {
__attribute__((used)) volatile amg::debug::RunStatus g_amg_run_status {
	amg::debug::run_status_magic,
	amg::debug::run_status_version,
	static_cast<amg::u16>(amg::debug::RunState::Cold),
	0,
	0,
};
}

namespace {

namespace drivers = amg::graphics::drivers;
namespace scene = amg::scene;
namespace tilemap = amg::graphics::tilemap;

constexpr amg::u16 map_tiles_x = 64;
constexpr amg::u16 map_tiles_y = 16;
constexpr amg::u16 surface_tiles_x = drivers::EhbTileScrollScene::surface_width / drivers::EhbTileScrollScene::tile_size;
constexpr amg::u16 tile_size = drivers::EhbTileScrollScene::tile_size;
constexpr amg::u8 tile_update_budget = 2;
constexpr amg::u16 animation_span_pixels = drivers::EhbTileScrollScene::prefetch_width - drivers::EhbTileScrollScene::tile_size;
constexpr amg::u16 logical_scroll_columns = map_tiles_x - surface_tiles_x + 1u;

constexpr drivers::EhbPalette sky_palette {{
	0x001, 0x014, 0x06e, 0x0af, 0x7df, 0xfff, 0xfd6, 0xff0,
	0x86b, 0xb9d, 0x263, 0x4a5, 0x6d7, 0xd8f, 0xf5b, 0x222,
	0x002, 0x025, 0x047, 0x069, 0x08b, 0x0ad, 0x4cf, 0x8ef,
	0x210, 0x431, 0x652, 0x873, 0xa94, 0xcb5, 0xed6, 0x333,
}};

constexpr drivers::EhbPalette jungle_palette {{
	0x000, 0x021, 0x063, 0x0a5, 0x2d7, 0xdfa, 0xce7, 0xff0,
	0x451, 0x783, 0x0f4, 0x4f8, 0x9fc, 0xfd7, 0xf6a, 0x222,
	0x010, 0x031, 0x052, 0x073, 0x094, 0x0b5, 0x3d7, 0x7f9,
	0x320, 0x541, 0x762, 0x983, 0xba4, 0xdc5, 0xfe6, 0x333,
}};

constexpr drivers::EhbPalette under_palette {{
	0x000, 0x112, 0x246, 0x48a, 0x7bd, 0xfff, 0xfc8, 0xf90,
	0x421, 0x742, 0x085, 0x0aa, 0x4dd, 0xf6c, 0xf3a, 0x221,
	0x100, 0x211, 0x322, 0x533, 0x744, 0x955, 0xb76, 0xd98,
	0x012, 0x124, 0x236, 0x348, 0x45a, 0x66c, 0x88e, 0x333,
}};

constexpr drivers::EhbPaletteZone palette_zones[] {
	{0x74, &jungle_palette},
	{0xb8, &under_palette},
};

void clear_bytes(amg::u8* bytes, amg::u32 count) {
	for (amg::u32 i = 0; i < count; ++i) {
		bytes[i] = 0;
	}
}

void build_virtual_map(tilemap::PackedTileCell* cells) {
	for (amg::u16 y = 0; y < map_tiles_y; ++y) {
		for (amg::u16 x = 0; x < map_tiles_x; ++x) {
			amg::u16 tile = 0;
			if (y < 3) {
				tile = static_cast<amg::u16>(((x + y * 5u) % 11u) == 0u ? 1u : 0u);
			} else if (y == 3) {
				tile = static_cast<amg::u16>(2u + ((x / 3u) & 1u));
			} else if (y < 8) {
				tile = static_cast<amg::u16>(4u + ((x + y) % 4u));
			} else if (y < 12) {
				tile = static_cast<amg::u16>(8u + (((x / 2u) + y) % 4u));
			} else {
				tile = static_cast<amg::u16>(12u + ((x + y * 3u) % 4u));
			}
			cells[static_cast<amg::u32>(y) * map_tiles_x + x].set_tile(tile);
		}
	}
}

void build_tile_word_cache(amg::u16* tile_words) {
	for (amg::u16 tile = 0; tile < 16; ++tile) {
		for (amg::u8 y = 0; y < tile_size; ++y) {
			amg::u8 color_a = 2;
			amg::u8 color_b = 3;
			amg::u8 color_accent = 5;
			amg::u16 mask_a = 0xffffu;
			amg::u16 mask_accent = 0x0000u;

			if (tile <= 1u) {
				mask_accent = (tile == 1u && y >= 5u && y <= 11u) ? 0x3ffcu : 0x0000u;
			} else if (tile <= 3u) {
				color_a = 8;
				color_b = 9;
				color_accent = 33;
				mask_a = y > 8u ? 0xffffu : static_cast<amg::u16>(0xffffu >> (8u - y));
				mask_accent = static_cast<amg::u16>(0x8000u >> ((y + tile * 3u) & 15u));
			} else if (tile <= 7u) {
				color_a = static_cast<amg::u8>(10u + (tile & 1u));
				color_b = static_cast<amg::u8>(42u + (tile & 1u));
				color_accent = (tile & 2u) ? 13u : 7u;
				mask_a = (y & 4u) ? 0x33ccu : 0xcc33u;
				mask_accent = static_cast<amg::u16>(0x1111u << (y & 3u));
			} else if (tile <= 11u) {
				color_a = 6;
				color_b = 39;
				color_accent = (tile & 1u) ? 14u : 15u;
				mask_a = (y & 4u) ? 0xaaaau : 0x5555u;
				mask_accent = (y == 0u || y == 15u) ? 0xffffu : static_cast<amg::u16>(0x8001u >> (y & 3u));
			} else {
				color_a = 3;
				color_b = 35;
				color_accent = (tile & 1u) ? 12u : 5u;
				mask_a = (y & 2u) ? 0xccccu : 0x3333u;
				mask_accent = static_cast<amg::u16>(0x00f0u << (y & 3u));
			}

			const amg::u16 mask_b = static_cast<amg::u16>(~(mask_a | mask_accent));
			for (amg::u8 plane = 0; plane < drivers::EhbTileScrollScene::plane_count; ++plane) {
				amg::u16 row = 0;
				if (color_a & (1u << plane)) {
					row |= static_cast<amg::u16>(mask_a & ~mask_accent);
				}
				if (color_b & (1u << plane)) {
					row |= mask_b;
				}
				if (color_accent & (1u << plane)) {
					row |= mask_accent;
				}
				tile_words[
					static_cast<amg::u32>(tile) * (drivers::EhbTileScrollScene::tile_bytes() / sizeof(amg::u16)) +
					static_cast<amg::u32>(plane) * tile_size +
					y
				] = row;
			}
		}
	}
}

const amg::u16* tile_source(const amg::MemoryBlock& block, amg::u16 tile_index) {
	return reinterpret_cast<const amg::u16*>(
		static_cast<const amg::u8*>(block.data) +
		static_cast<amg::u32>(tile_index & 15u) * drivers::EhbTileScrollScene::tile_bytes()
	);
}

void stamp_tile_cpu(
	drivers::EhbTileScrollScene& scene,
	const amg::u16* tile,
	amg::u16 surface_tile_x,
	amg::u16 surface_tile_y
) {
	for (amg::u8 plane = 0; plane < drivers::EhbTileScrollScene::plane_count; ++plane) {
		for (amg::u16 y = 0; y < tile_size; ++y) {
			const amg::u32 destination_offset =
				static_cast<amg::u32>(plane) * drivers::EhbTileScrollScene::plane_bytes +
				static_cast<amg::u32>(surface_tile_y * tile_size + y) * drivers::EhbTileScrollScene::surface_bytes_per_row +
				static_cast<amg::u32>(surface_tile_x) * sizeof(amg::u16);
			reinterpret_cast<amg::u16*>(scene.bitplanes() + destination_offset)[0] =
				tile[static_cast<amg::u32>(plane) * tile_size + y];
		}
	}
}

struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		if (!backend.configure_memory({112u * 1024u, 16u * 1024u, 8u * 1024u})) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000110u);
			return;
		}

		const drivers::EhbTileScrollConfig config {
			&sky_palette,
			palette_zones,
			static_cast<amg::u8>(sizeof(palette_zones) / sizeof(palette_zones[0])),
			1536,
		};
		if (!m_scene.init(backend.memory(), config)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000111u);
			return;
		}

		m_tiles = backend.memory().chip.allocate(drivers::EhbTileScrollScene::tile_bytes() * 16u, 16);
		if (!m_tiles.valid()) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000112u);
			return;
		}

		clear_bytes(m_scene.bitplanes(), drivers::EhbTileScrollScene::bitplane_bytes);
		build_virtual_map(m_cells);
		build_tile_word_cache(static_cast<amg::u16*>(m_tiles.data));
		m_map.reset(m_cells, map_tiles_x, map_tiles_y);

		for (amg::u16 y = 0; y < map_tiles_y; ++y) {
			for (amg::u16 x = 0; x < surface_tiles_x; ++x) {
				const amg::u16 tile = m_cells[static_cast<amg::u32>(y) * map_tiles_x + x].tile_index();
				stamp_tile_cpu(m_scene, tile_source(m_tiles, tile), x, y);
			}
		}

		// El anillo arranca con las primeras 24 columnas de mundo ya estampadas en
		// la superficie. Cuando la camara logica avance, `EhbHorizontalRingPrefetch`
		// detectara que un slot fisico debe reciclarse y pedira la columna nueva.
		m_scheduler.reset();
		m_ring.reset(0);

		if (!m_scene.rebuild_copper(0)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000114u);
			return;
		}

		m_scene.install(backend);
		publish_status(0, 0);
		m_ready = true;
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (m_ready) {
			const amg::u16 logical_column = logical_camera_column(context.frame.frame_index);
			schedule_next_ring_column(logical_column);
			const amg::u8 tile_jobs = upload_prefetch_tiles(backend);
			const amg::u16 camera_x = animated_camera_x(context.frame.frame_index);
			if (!m_scene.rebuild_copper(camera_x)) {
				amg::debug::mark_failed(g_amg_run_status, 0x00000115u);
				return;
			}
			m_scene.install(backend);
			publish_status(camera_x, tile_jobs);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_ready) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_ready = false;
	drivers::EhbTileScrollScene m_scene {};
	tilemap::PackedTileCell m_cells[map_tiles_x * map_tiles_y] {};
	tilemap::TileMap16 m_map {};
	tilemap::ProgressiveTileScheduler m_scheduler {};
	drivers::EhbHorizontalRingPrefetch m_ring {};
	amg::graphics::FramePlan m_frame_plan {};
	amg::MemoryBlock m_tiles {};
	amg::u16 m_pending_world_column = drivers::EhbHorizontalRingPrefetch::unknown_column;
	amg::u16 m_pending_surface_slot = 0;
	amg::u8 m_pending_rows = 0;
	amg::u8 m_recycled_columns = 0;

	/// Oscila dentro del margen de prefetch.
	///
	/// La ventana visible se mueve por `BPLCON1` y punteros de bitplane, no
	/// redibujando todo el fondo. El limite queda una columna antes del final para
	/// dejar siempre margen a los tiles que se estan preparando por Blitter.
	static constexpr amg::u16 animated_camera_x(amg::u32 frame_index) {
		const amg::u16 period = static_cast<amg::u16>(animation_span_pixels * 2u);
		const amg::u16 phase = static_cast<amg::u16>(frame_index % period);
		return phase <= animation_span_pixels ? phase : static_cast<amg::u16>(period - phase);
	}

	/// Camara logica de mundo usada solo para alimentar el anillo de prefetch.
	///
	/// El scroll visible de esta demo aun se mantiene en el tramo lineal seguro de
	/// la superficie; esta camara logica avanza mas lejos para demostrar que el
	/// driver puede reciclar slots y pedir columnas futuras sin que la capa de juego
	/// conozca el layout fisico.
	static constexpr amg::u16 logical_camera_column(amg::u32 frame_index) {
		return static_cast<amg::u16>((frame_index / tile_size) % logical_scroll_columns);
	}

	void schedule_next_ring_column(amg::u16 camera_world_column) {
		if (m_pending_world_column != drivers::EhbHorizontalRingPrefetch::unknown_column || m_scheduler.queued_count() != 0) {
			return;
		}

		tilemap::TileRect rect {};
		amg::u16 slot = 0;
		if (!m_ring.next_right_prefetch(camera_world_column, map_tiles_y, rect, slot)) {
			return;
		}

		m_scheduler.enqueue_strip(
			m_map,
			rect,
			tilemap::TileUpdateEdge::Right,
			0,
			12,
			1
		);
		m_pending_world_column = rect.left;
		m_pending_surface_slot = slot;
		m_pending_rows = static_cast<amg::u8>(rect.height);
	}

	/// Consume un presupuesto pequeno de tiles y lo ejecuta por Blitter.
	///
	/// Esta funcion es el embrion del futuro `TileScrollDriver::compile_frame`: la
	/// escena retenida dice que tiles urgen, el driver decide presupuesto, y el
	/// backend ejecuta trabajos concretos de Blitter sin que la logica de juego vea
	/// registros custom.
	amg::u8 upload_prefetch_tiles(amg::amiga::MinimalBackend& backend) {
		const tilemap::ProgressiveTileUpdatePlan plan = m_scheduler.take_budget(tile_update_budget);
		m_frame_plan.clear();
		for (amg::u8 i = 0; i < plan.count; ++i) {
			const tilemap::TileUpdateJob& job = plan.jobs[i];
			if (!m_frame_plan.add_tile_block_copy(m_scene.make_tile_upload_job(
				tile_source(m_tiles, job.tile_index),
				m_ring.slot_for_world_column(job.x),
				job.y
			))) {
				amg::debug::mark_failed(g_amg_run_status, 0x00000113u);
				return 0;
			}
		}

		if (plan.count != 0 && !backend.execute_frame_plan(m_frame_plan)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000116u);
			return 0;
		}
		if (m_pending_world_column != drivers::EhbHorizontalRingPrefetch::unknown_column && plan.count <= m_pending_rows) {
			m_pending_rows = static_cast<amg::u8>(m_pending_rows - plan.count);
			if (m_pending_rows == 0) {
				m_ring.mark_slot_ready(m_pending_surface_slot, m_pending_world_column);
				m_pending_world_column = drivers::EhbHorizontalRingPrefetch::unknown_column;
				++m_recycled_columns;
			}
		}
		return plan.count;
	}

	void publish_status(amg::u16 camera_x, amg::u8 tile_jobs) {
		amg::debug::mark_ready(
			g_amg_run_status,
			0x11000000u |
				(static_cast<amg::u32>(camera_x & 0xffu) << 16u) |
				(static_cast<amg::u32>(camera_x & 0x0fu) << 8u) |
				(static_cast<amg::u32>(tile_jobs & 0x0fu) << 4u) |
				static_cast<amg::u32>(m_recycled_columns & 0x0fu)
		);
	}
};

DemoGame g_game {};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	amg::debug::reset(g_amg_run_status);

	amg::amiga::MinimalBackend backend {};
	amg::Engine engine { backend, g_game };
	engine.run_frames(0xffff);

	return 0;
}
