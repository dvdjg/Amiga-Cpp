#include <amg/engine.hpp>
#include <amg/debug/run_status.hpp>
#include <amg/graphics/drivers/ehb_scene.hpp>
#include <amg/graphics/frame_plan.hpp>
#include <amg/platform/amiga_minimal.hpp>

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

constexpr amg::u16 screen_width = drivers::StaticEhbScene::width;
constexpr amg::u16 screen_height = drivers::StaticEhbScene::height;
constexpr amg::u16 bytes_per_row = drivers::StaticEhbScene::bytes_per_row;
constexpr amg::u8 plane_count = drivers::StaticEhbScene::plane_count;
constexpr amg::u32 plane_bytes = drivers::StaticEhbScene::plane_bytes;
constexpr amg::u16 bob_width = 32;
constexpr amg::u16 bob_height = 32;
constexpr amg::u16 bob_source_words_per_row = 3;
constexpr amg::u16 bob_destination_words_per_row = 3;
constexpr amg::u16 bob_shift = 9;
constexpr amg::u16 bob_x = 73;
constexpr amg::u16 bob_y = 96;
constexpr amg::u32 bob_plane_bytes = bob_source_words_per_row * sizeof(amg::u16) * bob_height;

constexpr drivers::EhbPalette palette {{
	0x000, 0x024, 0x048, 0x06c, 0xff0, 0xf80, 0x0ff, 0xf0f,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xfff, 0x111,
	0x012, 0x123, 0x234, 0x345, 0x456, 0x567, 0x678, 0x789,
	0x89a, 0x9ab, 0xabc, 0xbcd, 0xcde, 0xdef, 0xeee, 0x222,
}};

void add_reference_bars(amg::u8* planes) {
	// Fondo planar simple, deliberadamente no uniforme. El cookie-cut desplazado
	// debe conservar estas barras fuera de la mascara aunque el BOB empiece en una
	// posicion que no coincide con la frontera de word.
	for (amg::u16 y = 0; y < screen_height; ++y) {
		const amg::u32 row_offset = static_cast<amg::u32>(y) * bytes_per_row;
		for (amg::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
			const amg::u8 index = static_cast<amg::u8>(1u + ((byte_x / 8u) & 1u));
			const amg::u32 byte_index = row_offset + byte_x;
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u8* plane_base = planes + static_cast<amg::u32>(plane) * plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

void build_shift_source(amg::u16* mask, amg::u16* source) {
	for (amg::u16 y = 0; y < bob_height; ++y) {
		for (amg::u16 word = 0; word < bob_source_words_per_row; ++word) {
			const amg::u16 row_word = static_cast<amg::u16>(y * bob_source_words_per_row + word);
			mask[row_word] = 0;
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u16* plane_base = source + static_cast<amg::u32>(plane) * (bob_plane_bytes / sizeof(amg::u16));
				plane_base[row_word] = 0;
			}
		}

		for (amg::u16 x = 0; x < bob_width; ++x) {
			const amg::s16 dx = static_cast<amg::s16>(x - 15);
			const amg::s16 dy = static_cast<amg::s16>(y - 15);
			const bool inside = (dx * dx + dy * dy) <= 230;
			if (!inside) {
				continue;
			}

			const amg::u16 word = static_cast<amg::u16>(x / 16u);
			const amg::u16 bit = static_cast<amg::u16>(x & 15u);
			const amg::u16 bit_mask = static_cast<amg::u16>(0x8000u >> bit);
			const amg::u16 row_word = static_cast<amg::u16>(y * bob_source_words_per_row + word);
			const amg::u8 color_index = (dx * dx + dy * dy < 55) ? 14 : ((y & 4u) ? 6 : 4);

			mask[row_word] |= bit_mask;
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				if (color_index & (1u << plane)) {
					amg::u16* plane_base = source + static_cast<amg::u32>(plane) * (bob_plane_bytes / sizeof(amg::u16));
					plane_base[row_word] |= bit_mask;
				}
			}
		}
	}
}

amg::u16* destination_at(amg::u8* planes, amg::u16 x, amg::u16 y) {
	// El destino del Blitter sigue alineado a word; el desplazamiento fino vive en
	// `BlitJob::source_shift`. Para x=73, empezamos en la word de x=64 y pedimos al
	// Blitter que desplace mascara/datos 9 pixels.
	const amg::u16 aligned_x = static_cast<amg::u16>(x & 0xfff0u);
	const amg::u32 destination_offset =
		static_cast<amg::u32>(y) * bytes_per_row + (aligned_x / 8u);
	return reinterpret_cast<amg::u16*>(planes + destination_offset);
}

amg::graphics::BlitJob make_shifted_bob_job(
	const amg::u16* mask,
	const amg::u16* source,
	amg::u16* destination
) {
	return {
		amg::graphics::BlitJobKind::MaskedBobCookieCut,
		mask,
		source,
		destination,
		bob_destination_words_per_row,
		bob_height,
		0,
		static_cast<amg::s16>(bytes_per_row - bob_destination_words_per_row * sizeof(amg::u16)),
		plane_count,
		bob_shift,
		bob_plane_bytes,
		plane_bytes,
	};
}

struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		if (!backend.configure_memory({72u * 1024u, 8u * 1024u, 4u * 1024u})) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000051u);
			return;
		}

		const drivers::StaticEhbSceneConfig scene_config {
			&palette,
			nullptr,
			0,
			1024,
		};

		if (!m_scene.init(backend.memory(), scene_config)) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000052u);
			return;
		}

		add_reference_bars(m_scene.bitplanes());
		m_mask_block = backend.memory().chip.allocate(bob_plane_bytes, 16);
		m_source_block = backend.memory().chip.allocate(bob_plane_bytes * plane_count, 16);
		if (!m_mask_block.valid() || !m_source_block.valid()) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000053u);
			return;
		}

		build_shift_source(
			static_cast<amg::u16*>(m_mask_block.data),
			static_cast<amg::u16*>(m_source_block.data)
		);

		m_frame_plan.clear();
		const amg::graphics::BlitJob draw = make_shifted_bob_job(
			static_cast<const amg::u16*>(m_mask_block.data),
			static_cast<const amg::u16*>(m_source_block.data),
			destination_at(m_scene.bitplanes(), bob_x, bob_y)
		);

		if (!m_frame_plan.add_dirty_rect({
				static_cast<amg::s16>(bob_x),
				static_cast<amg::s16>(bob_y),
				static_cast<amg::s16>(bob_x + bob_width),
				static_cast<amg::s16>(bob_y + bob_height),
			}) ||
			!m_frame_plan.add_masked_bob(draw) ||
			!backend.execute_frame_plan(m_frame_plan)
		) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000054u);
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
				0x05100000u |
					(static_cast<amg::u32>(bob_shift) << 16u) |
					(static_cast<amg::u32>(bob_destination_words_per_row) << 8u) |
					static_cast<amg::u32>(m_frame_plan.blit_job_count())
			);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_ready = false;
	drivers::StaticEhbScene m_scene {};
	amg::graphics::FramePlan m_frame_plan {};
	amg::MemoryBlock m_mask_block {};
	amg::MemoryBlock m_source_block {};
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
