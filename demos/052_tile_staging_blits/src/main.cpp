#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/graphics/tilemap/tile_scroll.hpp>
#include <eng/platform/amiga_minimal.hpp>
#include <eng/scene/virtual_scene.hpp>

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

constexpr eng::u16 screen_width = drivers::StaticEhbScene::width;
constexpr eng::u16 screen_height = drivers::StaticEhbScene::height;
constexpr eng::u16 screen_bytes_per_row = drivers::StaticEhbScene::bytes_per_row;
constexpr eng::u8 plane_count = drivers::StaticEhbScene::plane_count;
constexpr eng::u32 screen_plane_bytes = drivers::StaticEhbScene::plane_bytes;
constexpr eng::u16 tile_size = 16;
constexpr eng::u16 tile_words_per_row = 1;
constexpr eng::u16 tile_count = 4;
constexpr eng::u16 staging_tiles_x = 4;
constexpr eng::u16 staging_tiles_y = 4;
constexpr eng::u16 staging_width = tile_size * tile_count;
constexpr eng::u16 staging_height = 64;
constexpr eng::u16 staging_words_per_row = staging_width / 16;
constexpr eng::u16 staging_bytes_per_row = staging_words_per_row * sizeof(eng::u16);
constexpr eng::u32 tile_plane_bytes = tile_words_per_row * sizeof(eng::u16) * tile_size;
constexpr eng::u32 tile_bytes = tile_plane_bytes * plane_count;
constexpr eng::u32 staging_plane_bytes = staging_bytes_per_row * staging_height;
constexpr eng::u16 present_x = 96;
constexpr eng::u16 present_y = 88;

constexpr drivers::EhbPalette palette {{
	0x000, 0x024, 0x048, 0x06c, 0xff0, 0xf80, 0x0ff, 0xf0f,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xfff, 0x111,
	0x012, 0x123, 0x234, 0x345, 0x456, 0x567, 0x678, 0x789,
	0x89a, 0x9ab, 0xabc, 0xbcd, 0xcde, 0xdef, 0xeee, 0x222,
}};

void add_reference_bars(eng::u8* planes) {
	for (eng::u16 y = 0; y < screen_height; ++y) {
		const eng::u32 row_offset = static_cast<eng::u32>(y) * screen_bytes_per_row;
		for (eng::u16 byte_x = 0; byte_x < screen_bytes_per_row; ++byte_x) {
			const eng::u8 index = static_cast<eng::u8>(1u + ((byte_x / 8u) & 1u));
			const eng::u32 byte_index = row_offset + byte_x;
			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				eng::u8* plane_base = planes + static_cast<eng::u32>(plane) * screen_plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

void clear_block(eng::u8* bytes, eng::u32 count) {
	for (eng::u32 i = 0; i < count; ++i) {
		bytes[i] = 0;
	}
}

void write_tile_pixel(eng::u16* tile, eng::u16 x, eng::u16 y, eng::u8 color_index) {
	const eng::u16 bit_mask = static_cast<eng::u16>(0x8000u >> (x & 15u));
	const eng::u16 row_word = static_cast<eng::u16>(y * tile_words_per_row);
	for (eng::u8 plane = 0; plane < plane_count; ++plane) {
		if (color_index & (1u << plane)) {
			eng::u16* plane_base = tile + static_cast<eng::u32>(plane) * (tile_plane_bytes / sizeof(eng::u16));
			plane_base[row_word] |= bit_mask;
		}
	}
}

void build_tile(eng::u16* tile, eng::u8 color_index, eng::u8 accent_index, eng::u8 pattern) {
	for (eng::u16 y = 0; y < tile_size; ++y) {
		for (eng::u16 x = 0; x < tile_size; ++x) {
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

eng::u16* tile_source_at(eng::MemoryBlock& block, eng::u16 tile_index) {
	return reinterpret_cast<eng::u16*>(
		static_cast<eng::u8*>(block.data) + static_cast<eng::u32>(tile_index) * tile_bytes
	);
}

eng::u16* staging_at(eng::MemoryBlock& block, eng::u16 x, eng::u16 y) {
	const eng::u32 offset = static_cast<eng::u32>(y) * staging_bytes_per_row + (x / 8u);
	return reinterpret_cast<eng::u16*>(static_cast<eng::u8*>(block.data) + offset);
}

eng::u16* screen_at(eng::u8* planes, eng::u16 x, eng::u16 y) {
	const eng::u32 offset = static_cast<eng::u32>(y) * screen_bytes_per_row + (x / 8u);
	return reinterpret_cast<eng::u16*>(planes + offset);
}

eng::graphics::BlitJob make_tile_upload_job(
	const eng::u16* source,
	eng::u16* destination
) {
	return {
		eng::graphics::BlitJobKind::TileBlockCopy,
		nullptr,
		source,
		destination,
		tile_words_per_row,
		tile_size,
		0,
		static_cast<eng::s16>(staging_bytes_per_row - tile_words_per_row * sizeof(eng::u16)),
		plane_count,
		0,
		tile_plane_bytes,
		staging_plane_bytes,
	};
}

eng::graphics::BlitJob make_present_staging_job(
	const eng::u16* source,
	eng::u16* destination
) {
	return {
		eng::graphics::BlitJobKind::CopyRect,
		nullptr,
		source,
		destination,
		staging_words_per_row,
		staging_height,
		0,
		static_cast<eng::s16>(screen_bytes_per_row - staging_bytes_per_row),
		plane_count,
		0,
		staging_plane_bytes,
		screen_plane_bytes,
	};
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({96u * 1024u, 8u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000052u);
			return;
		}

		const drivers::StaticEhbSceneConfig scene_config {
			&palette,
			nullptr,
			0,
			1024,
		};
		if (!m_scene.init(backend.memory(), scene_config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000053u);
			return;
		}

		add_reference_bars(m_scene.bitplanes());
		m_tiles_block = backend.memory().chip.allocate(tile_bytes * tile_count, 16);
		m_staging_block = backend.memory().chip.allocate(staging_plane_bytes * plane_count, 16);
		if (!m_tiles_block.valid() || !m_staging_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000054u);
			return;
		}

		clear_block(static_cast<eng::u8*>(m_tiles_block.data), tile_bytes * tile_count);
		clear_block(static_cast<eng::u8*>(m_staging_block.data), staging_plane_bytes * plane_count);
		build_tile(tile_source_at(m_tiles_block, 0), 4, 14, 0);
		build_tile(tile_source_at(m_tiles_block, 1), 6, 14, 1);
		build_tile(tile_source_at(m_tiles_block, 2), 7, 14, 2);
		build_tile(tile_source_at(m_tiles_block, 3), 5, 14, 3);

		m_frame_plan.clear();
		if (!prepare_tilemap_model()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000057u);
			return;
		}
		for (eng::u16 tile_y = 0; tile_y < staging_tiles_y; ++tile_y) {
			for (eng::u16 tile_x = 0; tile_x < staging_tiles_x; ++tile_x) {
				const eng::u16 tile_index = static_cast<eng::u16>((tile_x + tile_y) % tile_count);
				const eng::graphics::BlitJob tile_job = make_tile_upload_job(
					tile_source_at(m_tiles_block, tile_index),
					staging_at(
						m_staging_block,
						static_cast<eng::u16>(tile_x * tile_size),
						static_cast<eng::u16>(tile_y * tile_size)
					)
				);
				if (!m_frame_plan.add_tile_block_copy(tile_job)) {
					eng::debug::mark_failed(g_eng_run_status, 0x00000055u);
					return;
				}
			}
		}

		const eng::graphics::BlitJob present_job = make_present_staging_job(
			static_cast<const eng::u16*>(m_staging_block.data),
			screen_at(m_scene.bitplanes(), present_x, present_y)
		);
		if (!m_frame_plan.add_copy_rect(present_job) || !backend.execute_frame_plan(m_frame_plan)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000056u);
			return;
		}

		m_scene.install(backend);
		m_ready = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}

		m_scene.install(backend);
		if (context.frame.frame_index >= 3u) {
			eng::debug::mark_ready(
				g_eng_run_status,
				0x05200000u |
					(static_cast<eng::u32>(m_frame_plan.blit_budget().tile_jobs) << 12u) |
					(1u << 8u) |
					static_cast<eng::u32>(staging_words_per_row)
			);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool prepare_tilemap_model() {
		for (eng::u16 y = 0; y < staging_tiles_y; ++y) {
			for (eng::u16 x = 0; x < staging_tiles_x; ++x) {
				tilemap::PackedTileCell& cell = m_tile_cells[y * staging_tiles_x + x];
				cell.set_tile(static_cast<eng::u16>((x + y) % tile_count));
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
	eng::graphics::FramePlan m_frame_plan {};
	eng::MemoryBlock m_tiles_block {};
	eng::MemoryBlock m_staging_block {};
	tilemap::PackedTileCell m_tile_cells[staging_tiles_x * staging_tiles_y] {};
};

} // namespace

int main() {
	SysBase = *reinterpret_cast<struct ExecBase**>(4UL);
	eng::debug::reset(g_eng_run_status);

	eng::amiga::MinimalBackend backend {};
	DemoGame game {};
	eng::Engine engine { backend, game };
	engine.run_frames(0xffff);

	return 0;
}
