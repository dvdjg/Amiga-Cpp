#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/drivers/ehb_scene.hpp>
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

constexpr amg::u16 screen_width = drivers::StaticEhbScene::width;
constexpr amg::u16 screen_height = drivers::StaticEhbScene::height;
constexpr amg::u16 screen_bytes_per_row = drivers::StaticEhbScene::bytes_per_row;
constexpr amg::u8 plane_count = drivers::StaticEhbScene::plane_count;
constexpr amg::u32 screen_plane_bytes = drivers::StaticEhbScene::plane_bytes;
constexpr amg::u16 tile_size = 16;
constexpr amg::u16 tile_words_per_row = 1;
constexpr amg::u16 tile_count = 4;
constexpr amg::u16 staging_tiles_x = 4;
constexpr amg::u16 staging_tiles_y = 4;
constexpr amg::u16 staging_width = tile_size * tile_count;
constexpr amg::u16 staging_height = 64;
constexpr amg::u16 staging_words_per_row = staging_width / 16;
constexpr amg::u16 staging_bytes_per_row = staging_words_per_row * sizeof(amg::u16);
constexpr amg::u32 tile_plane_bytes = tile_words_per_row * sizeof(amg::u16) * tile_size;
constexpr amg::u32 tile_bytes = tile_plane_bytes * plane_count;
constexpr amg::u32 staging_plane_bytes = staging_bytes_per_row * staging_height;
constexpr amg::u16 present_x = 96;
constexpr amg::u16 present_y = 88;

constexpr drivers::EhbPalette palette {{
	0x000, 0x024, 0x048, 0x06c, 0xff0, 0xf80, 0x0ff, 0xf0f,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xfff, 0x111,
	0x012, 0x123, 0x234, 0x345, 0x456, 0x567, 0x678, 0x789,
	0x89a, 0x9ab, 0xabc, 0xbcd, 0xcde, 0xdef, 0xeee, 0x222,
}};

void add_reference_bars(amg::u8* planes) {
	for (amg::u16 y = 0; y < screen_height; ++y) {
		const amg::u32 row_offset = static_cast<amg::u32>(y) * screen_bytes_per_row;
		for (amg::u16 byte_x = 0; byte_x < screen_bytes_per_row; ++byte_x) {
			const amg::u8 index = static_cast<amg::u8>(1u + ((byte_x / 8u) & 1u));
			const amg::u32 byte_index = row_offset + byte_x;
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u8* plane_base = planes + static_cast<amg::u32>(plane) * screen_plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

void clear_block(amg::u8* bytes, amg::u32 count) {
	for (amg::u32 i = 0; i < count; ++i) {
		bytes[i] = 0;
	}
}

void write_tile_pixel(amg::u16* tile, amg::u16 x, amg::u16 y, amg::u8 color_index) {
	const amg::u16 bit_mask = static_cast<amg::u16>(0x8000u >> (x & 15u));
	const amg::u16 row_word = static_cast<amg::u16>(y * tile_words_per_row);
	for (amg::u8 plane = 0; plane < plane_count; ++plane) {
		if (color_index & (1u << plane)) {
			amg::u16* plane_base = tile + static_cast<amg::u32>(plane) * (tile_plane_bytes / sizeof(amg::u16));
			plane_base[row_word] |= bit_mask;
		}
	}
}

void build_tile(amg::u16* tile, amg::u8 color_index, amg::u8 accent_index, amg::u8 pattern) {
	for (amg::u16 y = 0; y < tile_size; ++y) {
		for (amg::u16 x = 0; x < tile_size; ++x) {
			bool accent = false;
			if (pattern == 0) {
				accent = x == y || x + y == tile_size - 1u;
			} else if (pattern == 1) {
				accent = (x & 3u) == 0u;
			} else if (pattern == 2) {
				accent = (y & 3u) == 0u;
			} else {
				accent = ((x / 4u) + (y / 4u)) & 1u;
			}
			write_tile_pixel(tile, x, y, accent ? accent_index : color_index);
		}
	}
}

amg::u16* tile_source_at(amg::MemoryBlock& block, amg::u16 tile_index) {
	return reinterpret_cast<amg::u16*>(
		static_cast<amg::u8*>(block.data) + static_cast<amg::u32>(tile_index) * tile_bytes
	);
}

amg::u16* staging_at(amg::MemoryBlock& block, amg::u16 x, amg::u16 y) {
	const amg::u32 offset = static_cast<amg::u32>(y) * staging_bytes_per_row + (x / 8u);
	return reinterpret_cast<amg::u16*>(static_cast<amg::u8*>(block.data) + offset);
}

amg::u16* screen_at(amg::u8* planes, amg::u16 x, amg::u16 y) {
	const amg::u32 offset = static_cast<amg::u32>(y) * screen_bytes_per_row + (x / 8u);
	return reinterpret_cast<amg::u16*>(planes + offset);
}

amg::graphics::BlitJob make_tile_upload_job(
	const amg::u16* source,
	amg::u16* destination
) {
	return {
		amg::graphics::BlitJobKind::TileBlockCopy,
		nullptr,
		source,
		destination,
		tile_words_per_row,
		tile_size,
		0,
		static_cast<amg::s16>(staging_bytes_per_row - tile_words_per_row * sizeof(amg::u16)),
		plane_count,
		0,
		tile_plane_bytes,
		staging_plane_bytes,
	};
}

amg::graphics::BlitJob make_present_staging_job(
	const amg::u16* source,
	amg::u16* destination
) {
	return {
		amg::graphics::BlitJobKind::CopyRect,
		nullptr,
		source,
		destination,
		staging_words_per_row,
		staging_height,
		0,
		static_cast<amg::s16>(screen_bytes_per_row - staging_bytes_per_row),
		plane_count,
		0,
		staging_plane_bytes,
		screen_plane_bytes,
	};
}

struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 8u * 1024u})) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000052u);
			return;
		}

		const drivers::StaticEhbSceneConfig scene_config {
			&palette,
			nullptr,
			0,
			1024,
		};
		if (!m_scene.init(backend.memory(), scene_config)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000053u);
			return;
		}

		add_reference_bars(m_scene.bitplanes());
		m_tiles_block = backend.memory().chip.allocate(tile_bytes * tile_count, 16);
		m_staging_block = backend.memory().chip.allocate(staging_plane_bytes * plane_count, 16);
		if (!m_tiles_block.valid() || !m_staging_block.valid()) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000054u);
			return;
		}

		clear_block(static_cast<amg::u8*>(m_tiles_block.data), tile_bytes * tile_count);
		clear_block(static_cast<amg::u8*>(m_staging_block.data), staging_plane_bytes * plane_count);
		build_tile(tile_source_at(m_tiles_block, 0), 4, 14, 0);
		build_tile(tile_source_at(m_tiles_block, 1), 6, 14, 1);
		build_tile(tile_source_at(m_tiles_block, 2), 7, 14, 2);
		build_tile(tile_source_at(m_tiles_block, 3), 5, 14, 3);

		m_frame_plan.clear();
		if (!prepare_tilemap_model()) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000057u);
			return;
		}
		for (amg::u16 tile_y = 0; tile_y < staging_tiles_y; ++tile_y) {
			for (amg::u16 tile_x = 0; tile_x < staging_tiles_x; ++tile_x) {
				const amg::u16 tile_index = static_cast<amg::u16>((tile_x + tile_y) % tile_count);
				const amg::graphics::BlitJob tile_job = make_tile_upload_job(
					tile_source_at(m_tiles_block, tile_index),
					staging_at(
						m_staging_block,
						static_cast<amg::u16>(tile_x * tile_size),
						static_cast<amg::u16>(tile_y * tile_size)
					)
				);
				if (!m_frame_plan.add_tile_block_copy(tile_job)) {
					amg::debug::mark_failed(g_amg_run_status, 0x00000055u);
					return;
				}
			}
		}

		const amg::graphics::BlitJob present_job = make_present_staging_job(
			static_cast<const amg::u16*>(m_staging_block.data),
			screen_at(m_scene.bitplanes(), present_x, present_y)
		);
		if (!m_frame_plan.add_copy_rect(present_job) || !backend.execute_frame_plan(m_frame_plan)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000056u);
			return;
		}

		m_scene.install(backend);
		m_ready = true;
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}

		m_scene.install(backend);
		if (context.frame.frame_index >= 3u) {
			amg::debug::mark_ready(
				g_amg_run_status,
				0x05200000u |
					(static_cast<amg::u32>(m_frame_plan.blit_budget().tile_jobs) << 12u) |
					(1u << 8u) |
					static_cast<amg::u32>(staging_words_per_row)
			);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool prepare_tilemap_model() {
		for (amg::u16 y = 0; y < staging_tiles_y; ++y) {
			for (amg::u16 x = 0; x < staging_tiles_x; ++x) {
				tilemap::PackedTileCell& cell = m_tile_cells[y * staging_tiles_x + x];
				cell.set_tile(static_cast<amg::u16>((x + y) % tile_count));
			}
		}

		tilemap::TileMap16 map { m_tile_cells, staging_tiles_x, staging_tiles_y };
		scene::Camera2D camera {};
		camera.reset(
			{0, 0, staging_width, staging_height},
			{staging_width, staging_height}
		);

		scene::TileLayer layer {
			"staging_pf1",
			&map,
			{
				scene::TileFramebufferStrategy::HiddenMargins,
				tilemap::ScrollAxes::Both,
				tile_size,
				0,
				0,
				16,
				1,
				true,
				false,
			},
			0,
			0,
		};

		scene::VirtualScene virtual_scene {};
		if (!virtual_scene.reset(camera, &layer, 1)) {
			return false;
		}

		const scene::VirtualSceneFrame frame = virtual_scene.prepare_frame();
		return !frame.overflow &&
			frame.tile_layer_count == 1u &&
			frame.tile_layers[0].scroll.dirty_tiles == staging_tiles_x * staging_tiles_y;
	}

	bool m_ready = false;
	drivers::StaticEhbScene m_scene {};
	amg::graphics::FramePlan m_frame_plan {};
	amg::MemoryBlock m_tiles_block {};
	amg::MemoryBlock m_staging_block {};
	tilemap::PackedTileCell m_tile_cells[staging_tiles_x * staging_tiles_y] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	amg::debug::reset(g_amg_run_status);

	amg::amiga::MinimalBackend backend {};
	DemoGame game {};
	amg::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
