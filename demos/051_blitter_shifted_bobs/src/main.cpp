#include <eng/engine.hpp>
#include <eng/debug/run_status.hpp>
#include <eng/graphics/drivers/ehb_scene.hpp>
#include <eng/graphics/frame_plan.hpp>
#include <eng/platform/amiga_minimal.hpp>

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

constexpr eng::u16 screen_width = drivers::StaticEhbScene::width;
constexpr eng::u16 screen_height = drivers::StaticEhbScene::height;
constexpr eng::u16 bytes_per_row = drivers::StaticEhbScene::bytes_per_row;
constexpr eng::u8 plane_count = drivers::StaticEhbScene::plane_count;
constexpr eng::u32 plane_bytes = drivers::StaticEhbScene::plane_bytes;
constexpr eng::u16 bob_width = 32;
constexpr eng::u16 bob_height = 32;
constexpr eng::u16 bob_source_words_per_row = 3;
constexpr eng::u16 bob_destination_words_per_row = 3;
constexpr eng::u16 bob_shift = 9;
constexpr eng::u16 bob_x = 73;
constexpr eng::u16 bob_y = 96;
constexpr eng::u32 bob_plane_bytes = bob_source_words_per_row * sizeof(eng::u16) * bob_height;

constexpr drivers::EhbPalette palette {{
	0x000, 0x024, 0x048, 0x06c, 0xff0, 0xf80, 0x0ff, 0xf0f,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xfff, 0x111,
	0x012, 0x123, 0x234, 0x345, 0x456, 0x567, 0x678, 0x789,
	0x89a, 0x9ab, 0xabc, 0xbcd, 0xcde, 0xdef, 0xeee, 0x222,
}};

void add_reference_bars(eng::u8* planes) {
	// Fondo planar simple, deliberadamente no uniforme. El cookie-cut desplazado
	// debe conservar estas barras fuera de la mascara aunque el BOB empiece en una
	// posicion que no coincide con la frontera de word.
	for (eng::u16 y = 0; y < screen_height; ++y) {
		const eng::u32 row_offset = static_cast<eng::u32>(y) * bytes_per_row;
		for (eng::u16 byte_x = 0; byte_x < bytes_per_row; ++byte_x) {
			const eng::u8 index = static_cast<eng::u8>(1u + ((byte_x / 8u) & 1u));
			const eng::u32 byte_index = row_offset + byte_x;
			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				eng::u8* plane_base = planes + static_cast<eng::u32>(plane) * plane_bytes;
				plane_base[byte_index] = (index & (1u << plane)) ? 0xffu : 0x00u;
			}
		}
	}
}

void build_shift_source(eng::u16* mask, eng::u16* source) {
	for (eng::u16 y = 0; y < bob_height; ++y) {
		for (eng::u16 word = 0; word < bob_source_words_per_row; ++word) {
			const eng::u16 row_word = static_cast<eng::u16>(y * bob_source_words_per_row + word);
			mask[row_word] = 0;
			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				eng::u16* plane_base = source + static_cast<eng::u32>(plane) * (bob_plane_bytes / sizeof(eng::u16));
				plane_base[row_word] = 0;
			}
		}

		for (eng::u16 x = 0; x < bob_width; ++x) {
			const eng::s16 dx = static_cast<eng::s16>(x - 15);
			const eng::s16 dy = static_cast<eng::s16>(y - 15);
			const bool inside = (dx * dx + dy * dy) <= 230;
			if (!inside) {
				continue;
			}

			const eng::u16 word = static_cast<eng::u16>(x / 16u);
			const eng::u16 bit = static_cast<eng::u16>(x & 15u);
			const eng::u16 bit_mask = static_cast<eng::u16>(0x8000u >> bit);
			const eng::u16 row_word = static_cast<eng::u16>(y * bob_source_words_per_row + word);
			const eng::u8 color_index = (dx * dx + dy * dy < 55) ? 14 : ((y & 4u) ? 6 : 4);

			mask[row_word] |= bit_mask;
			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				if (color_index & (1u << plane)) {
					eng::u16* plane_base = source + static_cast<eng::u32>(plane) * (bob_plane_bytes / sizeof(eng::u16));
					plane_base[row_word] |= bit_mask;
				}
			}
		}
	}
}

eng::u16* destination_at(eng::u8* planes, eng::u16 x, eng::u16 y) {
	// El destino del Blitter sigue alineado a word; el desplazamiento fino vive en
	// `BlitJob::source_shift`. Para x=73, empezamos en la word de x=64 y pedimos al
	// Blitter que desplace mascara/datos 9 pixels.
	const eng::u16 aligned_x = static_cast<eng::u16>(x & 0xfff0u);
	const eng::u32 destination_offset =
		static_cast<eng::u32>(y) * bytes_per_row + (aligned_x / 8u);
	return reinterpret_cast<eng::u16*>(planes + destination_offset);
}

eng::graphics::BlitJob make_shifted_bob_job(
	const eng::u16* mask,
	const eng::u16* source,
	eng::u16* destination
) {
	return {
		eng::graphics::BlitJobKind::MaskedBobCookieCut,
		mask,
		source,
		destination,
		bob_destination_words_per_row,
		bob_height,
		0,
		static_cast<eng::s16>(bytes_per_row - bob_destination_words_per_row * sizeof(eng::u16)),
		plane_count,
		bob_shift,
		bob_plane_bytes,
		plane_bytes,
	};
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
		if (!backend.configure_memory({72u * 1024u, 8u * 1024u, 4u * 1024u})) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000051u);
			return;
		}

		const drivers::StaticEhbSceneConfig scene_config {
			&palette,
			nullptr,
			0,
			1024,
		};

		if (!m_scene.init(backend.memory(), scene_config)) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000052u);
			return;
		}

		add_reference_bars(m_scene.bitplanes());
		m_mask_block = backend.memory().chip.allocate(bob_plane_bytes, 16);
		m_source_block = backend.memory().chip.allocate(bob_plane_bytes * plane_count, 16);
		if (!m_mask_block.valid() || !m_source_block.valid()) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000053u);
			return;
		}

		build_shift_source(
			static_cast<eng::u16*>(m_mask_block.data),
			static_cast<eng::u16*>(m_source_block.data)
		);

		m_frame_plan.clear();
		const eng::graphics::BlitJob draw = make_shifted_bob_job(
			static_cast<const eng::u16*>(m_mask_block.data),
			static_cast<const eng::u16*>(m_source_block.data),
			destination_at(m_scene.bitplanes(), bob_x, bob_y)
		);

		if (!m_frame_plan.add_dirty_rect({
				static_cast<eng::s16>(bob_x),
				static_cast<eng::s16>(bob_y),
				static_cast<eng::s16>(bob_x + bob_width),
				static_cast<eng::s16>(bob_y + bob_height),
			}) ||
			!m_frame_plan.add_masked_bob(draw) ||
			!backend.execute_frame_plan(m_frame_plan)
		) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000054u);
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
				0x05100000u |
					(static_cast<eng::u32>(bob_shift) << 16u) |
					(static_cast<eng::u32>(bob_destination_words_per_row) << 8u) |
					static_cast<eng::u32>(m_frame_plan.blit_job_count())
			);
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool m_ready = false;
	drivers::StaticEhbScene m_scene {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::MemoryBlock m_mask_block {};
	eng::MemoryBlock m_source_block {};
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
