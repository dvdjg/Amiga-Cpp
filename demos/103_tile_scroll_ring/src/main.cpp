#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/drivers/tile_scroll.hpp>
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

namespace drivers = eng::graphics::drivers;

/// EHB single con superficie ring 352x288 (viewport 320x256 + 2 tiles de margen
/// por eje). El buffer se recicla al cruzar el borde: un blit desplaza el
/// contenido 16px hacia el lado contrario y se re-dibujan los tiles nuevos que
/// entran (scroll estilo Lionheart, sin superficie lineal grande).
constexpr auto kMode = drivers::TileScrollMode::ehb();
using Scene = drivers::TileScrollScene<kMode, 2, 2>;

constexpr eng::u16 tile_size = Scene::tile_size;
constexpr eng::u16 surface_tiles_x = Scene::surface_width / tile_size;   // 22
constexpr eng::u16 surface_tiles_y = Scene::surface_height / tile_size;  // 18
constexpr eng::u16 surface_bytes_per_row = Scene::surface_bytes_per_row; // 44
constexpr eng::u32 plane_bytes = Scene::plane_bytes;

constexpr eng::u16 map_tiles_x = 256;
constexpr eng::u16 map_tiles_y = 128;
constexpr eng::u16 tile_pattern_count = 64;
constexpr eng::u16 tile_update_budget = 18;

constexpr drivers::EhbPalette ring_palette {{
	0x000, 0x222, 0x08f, 0x0cf, 0xf0c, 0xff0, 0x0f4, 0xf80,
	0x84f, 0x0a6, 0xf44, 0x6df, 0xf8f, 0xfff, 0x888, 0x444,
	0x000, 0x111, 0x048, 0x068, 0x806, 0x880, 0x082, 0x840,
	0x426, 0x053, 0x822, 0x368, 0x846, 0x888, 0x444, 0x222,
}};

constexpr drivers::EhbPaletteZone palette_zones[] {};

// --- Glifos y tiles simbolicos (igual que la demo 101) -----------------------

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

constexpr eng::u16 symbolic_tile_plane_row(eng::u8 glyph, eng::u8 variant, eng::u8 y, eng::u8 plane) {
	constexpr eng::u8 backgrounds[] {3, 8, 10, 12};
	constexpr eng::u8 glyph_colors[] {5, 7, 13, 14};
	constexpr eng::u8 border_colors[] {15, 6, 9, 11};
	const eng::u8 bg = backgrounds[variant & 3u];
	const eng::u8 ink = glyph_colors[variant & 3u];
	const eng::u8 border = border_colors[variant & 3u];

	const eng::u16 border_mask = (y == 0u || y == 15u) ? 0xffffu : 0x8001u;
	const eng::u16 marker_mask = static_cast<eng::u16>(variant_marker_mask(variant, y) & ~border_mask);
	const eng::u16 glyph_mask = static_cast<eng::u16>(glyph_row_mask(glyph, y) & ~(border_mask | marker_mask));
	const eng::u16 bg_mask = static_cast<eng::u16>(~(border_mask | marker_mask | glyph_mask));

	eng::u16 row = 0;
	if ((bg & (1u << plane)) != 0u) {
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

constexpr eng::u16 world_tile(eng::u32 col, eng::u32 row) {
	const eng::u32 x = col % map_tiles_x;
	const eng::u32 y = row % map_tiles_y;
	eng::u32 h = x * 0x9e3779b9u ^ y * 0x85ebca6bu ^ 0x1021u;
	h ^= h >> 16u;
	h *= 0x7feb352du;
	h ^= h >> 15u;
	const eng::u16 symbol = static_cast<eng::u16>(h & 0x0fu);
	const eng::u16 variant = static_cast<eng::u16>((h >> 4u) & 3u);
	return static_cast<eng::u16>(symbol | (variant << 4u));
}

eng::Span<const eng::u16> tile_source(const eng::MemoryBlock& block, eng::u16 tile_index) {
	constexpr eng::u32 words_per_tile = Scene::tile_bytes() / sizeof(eng::u16);
	const eng::u32 word_offset = static_cast<eng::u32>(tile_index & (tile_pattern_count - 1u)) * words_per_tile;
	return {
		reinterpret_cast<const eng::u16*>(static_cast<const eng::u8*>(block.data)) + word_offset,
		words_per_tile,
	};
}

void build_tile_word_cache(eng::Span<eng::u16> tile_words) {
	constexpr eng::u32 words_per_tile = Scene::tile_bytes() / sizeof(eng::u16);
	for (eng::u16 tile = 0; tile < tile_pattern_count; ++tile) {
		const eng::u8 glyph = static_cast<eng::u8>(tile & 15u);
		const eng::u8 variant = static_cast<eng::u8>((tile >> 4u) & 3u);
		for (eng::u8 y = 0; y < tile_size; ++y) {
			for (eng::u8 plane = 0; plane < Scene::plane_count; ++plane) {
				tile_words.at(
					static_cast<eng::u32>(tile) * words_per_tile +
					static_cast<eng::u32>(plane) * tile_size +
					y
				) = symbolic_tile_plane_row(glyph, variant, y, plane);
			}
		}
	}
}

// --- Camara de anillo: desplaza el viewport por el mundo y recicla el buffer ---

/// Camara de scroll infinito sobre el buffer ring de 22x18 tiles.
///
/// `wx`/`wy` son la posicion de mundo (pixels, sin limite). El display apunta a
/// `px`/`py` en [1,16]: la parte fina del scroll dentro del primer tile del
/// buffer. Cuando `wx` cruza un multiplo de 16, el buffer se desplaza un tile
/// (vdx/vdy = +-1) y el resto de la demo recarga los tiles que entran.
struct RingCamera {
	eng::u32 wx = 1;
	eng::u32 wy = 1;
	eng::u32 view_x = 0;
	eng::u32 view_y = 0;
	eng::u16 px = 1;
	eng::u16 py = 1;

	eng::s8 vdx = 0;
	eng::s8 vdy = 0;

	static constexpr eng::u32 pause_frames = 30;
	static constexpr eng::u32 phase_dur[7] {400, 400, 320, 320, 480, 480, 480};
	static constexpr eng::u32 jump_start = 30 * 7u + 400 + 400 + 320 + 320 + 480 + 480 + 480;
	static constexpr eng::u32 repattern_frames = 500;

	eng::u8 m_phase = 0xffu;
	eng::u32 m_phase_wx = 1;
	eng::u32 m_phase_wy = 1;
	eng::s8 m_h_dir = 1;
	eng::s8 m_v_dir = 1;
	eng::u32 m_jump_epoch = 0xffffffffu;
	eng::u32 m_rng = 0x13579bdu;

	void advance(eng::u32 frame_index) {
		if (frame_index >= jump_start) {
			advance_jump(frame_index);
		} else {
			advance_phases(frame_index);
		}
		derive();
	}

private:
	static constexpr eng::s16 circle_offset_x(eng::u8 index) {
		constexpr eng::s16 offsets[] {
			64, 64, 63, 61, 59, 56, 53, 49,
			45, 41, 36, 31, 24, 18, 12, 6,
			0, -6, -12, -18, -24, -31, -36, -41,
			-45, -49, -53, -56, -59, -61, -63, -64,
			-64, -64, -63, -61, -59, -56, -53, -49,
			-45, -41, -36, -31, -24, -18, -12, -6,
			0, 6, 12, 18, 24, 31, 36, 41,
			45, 49, 53, 56, 59, 61, 63, 64,
		};
		return offsets[index & 63u];
	}

	static constexpr eng::s16 circle_offset_y(eng::u8 index) {
		constexpr eng::s16 offsets[] {
			0, 6, 12, 18, 24, 31, 36, 41,
			45, 49, 53, 56, 59, 61, 63, 64,
			64, 64, 63, 61, 59, 56, 53, 49,
			45, 41, 36, 31, 24, 18, 12, 6,
			0, -6, -12, -18, -24, -31, -36, -41,
			-45, -49, -53, -56, -59, -61, -63, -64,
			-64, -64, -63, -61, -59, -56, -53, -49,
			-45, -41, -36, -31, -24, -18, -12, -6,
		};
		return offsets[index & 63u];
	}

	static constexpr eng::u32 sub_u32(eng::u32 a, eng::u32 b) {
		return a >= b ? a - b : 0;
	}

	static constexpr eng::s16 radius_signed(eng::s16 offset) {
		return static_cast<eng::s16>(96 * offset / 64);
	}

	void advance_phases(eng::u32 frame_index) {
		eng::u32 t = frame_index;
		for (eng::u8 p = 0; p < 7; ++p) {
			const eng::u32 block = pause_frames + phase_dur[p];
			if (t < pause_frames) {
				return; // paron: la camara se queda quieta
			}
			t -= pause_frames;
			if (t < phase_dur[p]) {
				if (p != m_phase) {
					m_phase = p;
					m_phase_wx = wx;
					m_phase_wy = wy;
				}
				switch (p) {
				case 0: wx = m_phase_wx + t; wy = m_phase_wy; break;
				case 1: wx = sub_u32(m_phase_wx, t); wy = m_phase_wy; break;
				case 2: wx = m_phase_wx; wy = m_phase_wy + t; break;
				case 3: wx = m_phase_wx; wy = sub_u32(m_phase_wy, t); break;
				case 4: wx = m_phase_wx + t; wy = m_phase_wy + t; break;
				case 5: {
					const eng::u8 a = static_cast<eng::u8>((t * 64u) / phase_dur[5]);
					wx = m_phase_wx + radius_signed(circle_offset_x(a));
					wy = m_phase_wy + radius_signed(circle_offset_y(a));
					break;
				}
				case 6: {
					const eng::u8 a = static_cast<eng::u8>((t * 128u) / phase_dur[6]);
					wx = m_phase_wx + t;
					wy = m_phase_wy + radius_signed(circle_offset_y(a));
					break;
				}
				}
				return;
			}
			t -= phase_dur[p];
		}
	}

	void advance_jump(eng::u32 frame_index) {
		const eng::u32 epoch = (frame_index - jump_start) / repattern_frames;
		if (epoch != m_jump_epoch) {
			m_jump_epoch = epoch;
			m_h_dir = (rng_next() & 1u) != 0u ? 1 : -1;
			m_v_dir = (rng_next() & 1u) != 0u ? 1 : -1;
		}
		const eng::u32 step = 2 + (rng_next() % 14u);
		wx += static_cast<eng::u32>(static_cast<eng::s32>(step) * m_h_dir);
		wy += static_cast<eng::u32>(static_cast<eng::s32>(step) * m_v_dir);
	}

	void derive() {
		const eng::u32 nvx = wx / 16u;
		const eng::u32 nvy = wy / 16u;
		vdx = 0;
		vdy = 0;
		if (nvx != view_x) {
			vdx = nvx > view_x ? 1 : -1;
		}
		if (nvy != view_y) {
			vdy = nvy > view_y ? 1 : -1;
		}
		view_x = nvx;
		view_y = nvy;
		px = static_cast<eng::u16>((wx % 16u) + 1u);
		py = static_cast<eng::u16>((wy % 16u) + 1u);
	}

	eng::u32 rng_next() {
		eng::u32 z = m_rng;
		z ^= z << 13u;
		z ^= z >> 17u;
		z ^= z << 5u;
		m_rng = z;
		return z;
	}
};

// --- Demo ----------------------------------------------------------------------

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({120u * 1024u, 16u * 1024u, 8u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000310u);
			return;
		}

		const drivers::TileScrollConfig config {&ring_palette, palette_zones, 0, 1536};
		if (!m_scene.init(backend.memory(), config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000311u);
			return;
		}

		m_tiles = backend.memory().chip.allocate(Scene::tile_bytes() * tile_pattern_count, 16);
		if (!m_tiles.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000312u);
			return;
		}
		m_scratch = backend.memory().chip.allocate(
			static_cast<eng::u32>(surface_bytes_per_row - (tile_size / 8u)) * Scene::surface_height,
			16
		);
		if (!m_scratch.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000313u);
			return;
		}

		m_scene.bitplane_span().clear();
		build_tile_word_cache(eng::Span<eng::u16>::from_raw(
			static_cast<eng::u16*>(m_tiles.data),
			m_tiles.size / sizeof(eng::u16)
		));

		// El buffer arranca poblado con el mundo visible desde (0,0).
		for (eng::u16 ty = 0; ty < surface_tiles_y; ++ty) {
			for (eng::u16 tx = 0; tx < surface_tiles_x; ++tx) {
				stamp_tile(tx, ty, tx, ty);
			}
		}

		if (!m_scene.rebuild_copper(m_cam.px, m_cam.py)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000314u);
			return;
		}
		m_scene.install(backend);
		publish_status(0);
		m_ready = true;
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_ready) {
			return;
		}

		m_cam.advance(context.frame.frame_index);

		if (m_cam.vdx != 0 || m_cam.vdy != 0) {
			m_frame_plan.clear();
			configure_budget(m_frame_plan);
			add_shift(m_frame_plan, m_cam.vdx, m_cam.vdy);
			add_edges(m_frame_plan, m_cam.vdx, m_cam.vdy);
			if (m_frame_plan.blit_budget_report().status == eng::graphics::BlitBudgetStatus::Exceeded) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000317u);
				return;
			}
			if (!backend.execute_frame_plan(m_frame_plan)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000316u);
				return;
			}
		}

		if (!m_scene.rebuild_copper(m_cam.px, m_cam.py)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000315u);
			return;
		}
		publish_status(m_cam.vdx != 0 || m_cam.vdy != 0 ? 1u : 0u);
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_ready) {
			m_scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

private:
	static void configure_budget(eng::graphics::FramePlan& plan) {
		// Un cruce de borde ejecuta el shift en dos blits por plano via scratch
		// (2 x 21x288x6 = 72K words) mas los tiles de borde (~3.8K words).
		plan.set_blit_budget_limits({16384, 131072, 64, 160});
	}

	/// Blit de desplazamiento del buffer un tile (16px) hacia el lado contrario.
	/// Shift del ring en dos blits no-solapados via scratch (el CopyRect solapado
	/// no mueve el buffer en WinUAE-DBG). El scratch cabe por plano.
	void add_shift(eng::graphics::FramePlan& plan, eng::s8 vdx, eng::s8 vdy) {
		const eng::u8* base = m_scene.bitplanes();
		const eng::u32 src_dx = vdx > 0 ? 2u : 0u;
		const eng::u32 dst_dx = vdx < 0 ? 2u : 0u;
		const eng::u32 src_dy = vdy > 0 ? surface_bytes_per_row * 16u : 0u;
		const eng::u32 dst_dy = vdy < 0 ? surface_bytes_per_row * 16u : 0u;
		const eng::u16 width_bytes = vdx != 0
			? static_cast<eng::u16>(surface_bytes_per_row - (tile_size / 8u))
			: surface_bytes_per_row;
		const eng::u16 height = vdy != 0 ? static_cast<eng::u16>(Scene::surface_height - tile_size) : Scene::surface_height;
		const eng::u16 words = static_cast<eng::u16>(width_bytes / 2u);
		const eng::s16 src_mod = static_cast<eng::s16>(surface_bytes_per_row - width_bytes);
		eng::u8* scratch = static_cast<eng::u8*>(m_scratch.data);
		for (eng::u8 pl = 0; pl < Scene::plane_count; ++pl) {
			plan.add_tile_block_copy({
				eng::graphics::BlitJobKind::CopyRect,
				nullptr,
				reinterpret_cast<const eng::u16*>(base + pl * plane_bytes + src_dx + src_dy),
				reinterpret_cast<eng::u16*>(scratch),
				words,
				height,
				src_mod,
				0,
				1,
				0,
				plane_bytes,
				width_bytes,
				false,
			});
			plan.add_tile_block_copy({
				eng::graphics::BlitJobKind::CopyRect,
				nullptr,
				reinterpret_cast<const eng::u16*>(scratch),
				reinterpret_cast<eng::u16*>(m_scene.bitplanes() + pl * plane_bytes + dst_dx + dst_dy),
				words,
				height,
				0,
				src_mod,
				1,
				0,
				width_bytes,
				plane_bytes,
				false,
			});
		}
	}

	/// Recarga los tiles del mundo que entran por los bordes tras el cruce.
	void add_edges(eng::graphics::FramePlan& plan, eng::s8 vdx, eng::s8 vdy) {
		if (vdx != 0) {
			const eng::u16 slot_col = vdx > 0 ? static_cast<eng::u16>(surface_tiles_x - 1u) : 0u;
			const eng::u32 world_col = vdx > 0 ? m_cam.view_x + surface_tiles_x - 1u : m_cam.view_x;
			for (eng::u16 ty = 0; ty < surface_tiles_y; ++ty) {
				const eng::u32 world_row = m_cam.view_y + ty;
				plan.add_tile_block_copy(m_scene.make_tile_upload_job(
					tile_source(m_tiles, world_tile(world_col, world_row)).data(),
					slot_col,
					ty
				));
			}
		}
		if (vdy != 0) {
			const eng::u16 slot_row = vdy > 0 ? static_cast<eng::u16>(surface_tiles_y - 1u) : 0u;
			const eng::u32 world_row = vdy > 0 ? m_cam.view_y + surface_tiles_y - 1u : m_cam.view_y;
			for (eng::u16 tx = 0; tx < surface_tiles_x; ++tx) {
				const eng::u32 world_col = m_cam.view_x + tx;
				plan.add_tile_block_copy(m_scene.make_tile_upload_job(
					tile_source(m_tiles, world_tile(world_col, world_row)).data(),
					tx,
					slot_row
				));
			}
		}
	}

	void stamp_tile(eng::u16 world_col, eng::u16 world_row, eng::u16 surface_tile_x, eng::u16 surface_tile_y) {
		for (eng::u8 plane = 0; plane < Scene::plane_count; ++plane) {
			for (eng::u16 y = 0; y < tile_size; ++y) {
				const eng::u32 row_offset =
					static_cast<eng::u32>(surface_tile_y * tile_size + y) *
					(surface_bytes_per_row / sizeof(eng::u16)) +
					surface_tile_x;
				m_scene.plane_words(plane).at(row_offset) =
					tile_source(m_tiles, world_tile(world_col, world_row))
						.at(static_cast<eng::u32>(plane) * tile_size + y);
			}
		}
	}

	void publish_status(eng::u32 wrap) {
		eng::debug::mark_ready(
			g_eng_run_status,
			0x13000000u |
				(static_cast<eng::u32>(m_cam.px & 0xffu) << 16u) |
				(static_cast<eng::u32>(m_cam.py & 0xffu) << 8u) |
				(static_cast<eng::u32>(wrap & 0x0fu) << 4u)
		);
	}

	bool m_ready = false;
	Scene m_scene {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::MemoryBlock m_tiles {};
	eng::MemoryBlock m_scratch {};
	RingCamera m_cam {};
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
