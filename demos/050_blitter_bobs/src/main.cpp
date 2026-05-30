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
constexpr amg::u16 bob_words_per_row = bob_width / 16;
constexpr amg::u32 bob_plane_bytes = bob_words_per_row * sizeof(amg::u16) * bob_height;
constexpr amg::u16 bob_start_x = 48; // Multiplo de 16: esta fase no usa shifts.
constexpr amg::u16 bob_y = 92;
constexpr amg::u16 blob_left_x = 48;
constexpr amg::u16 blob_right_x = 224;
constexpr amg::u16 blob_y = 150;

/// Paleta sencilla para distinguir fondo y BOB.
///
/// Seguimos en EHB aunque esta demo solo necesite pocos colores porque queremos
/// validar que el Blitter trabaja sobre el mismo layout de 6 bitplanes que usaran
/// aventuras EHB y escenas ricas.
constexpr drivers::EhbPalette palette {{
	0x000, 0x024, 0x048, 0x06c, 0xff0, 0xf80, 0x0ff, 0xf0f,
	0x246, 0x468, 0x68a, 0x8ac, 0xace, 0xcdf, 0xfff, 0x111,
	0x012, 0x123, 0x234, 0x345, 0x456, 0x567, 0x678, 0x789,
	0x89a, 0x9ab, 0xabc, 0xbcd, 0xcde, 0xdef, 0xeee, 0x222,
}};

void fill_screen_with_index(amg::u8* planes, amg::u8 index) {
	for (amg::u8 plane = 0; plane < plane_count; ++plane) {
		const amg::u8 value = (index & (1u << plane)) ? 0xffu : 0x00u;
		amg::u8* plane_base = planes + static_cast<amg::u32>(plane) * plane_bytes;
		for (amg::u32 i = 0; i < plane_bytes; ++i) {
			plane_base[i] = value;
		}
	}
}

void add_reference_bars(amg::u8* planes) {
	// Barras verticales suaves en el fondo. El BOB debe cortarlas limpiamente; si
	// el minterm del Blitter estuviera mal, el analizador detectaria contaminacion
	// de colores o ausencia del blanco central.
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

void build_masked_bob(amg::u16* mask, amg::u16* source, amg::u8 outer_color, amg::u8 inner_color) {
	for (amg::u16 y = 0; y < bob_height; ++y) {
		for (amg::u16 word = 0; word < bob_words_per_row; ++word) {
			const amg::u16 row_word = static_cast<amg::u16>(y * bob_words_per_row + word);
			amg::u16 mask_word = 0;
			amg::u16 source_words[plane_count] {};

			for (amg::u16 bit = 0; bit < 16; ++bit) {
				const amg::s16 x = static_cast<amg::s16>(word * 16u + bit);
				const amg::s16 dx = static_cast<amg::s16>(x - 15);
				const amg::s16 dy = static_cast<amg::s16>(y - 15);
				const bool inside = (dx * dx + dy * dy) <= 230;
				if (!inside) {
					continue;
				}

				const amg::u16 bit_mask = static_cast<amg::u16>(0x8000u >> bit);
				const amg::u8 color_index = (dx * dx + dy * dy < 70) ? inner_color : outer_color;
				mask_word |= bit_mask;
				for (amg::u8 plane = 0; plane < plane_count; ++plane) {
					if (color_index & (1u << plane)) {
						source_words[plane] |= bit_mask;
					}
				}
			}

			mask[row_word] = mask_word;
			for (amg::u8 plane = 0; plane < plane_count; ++plane) {
				amg::u16* plane_base = source + static_cast<amg::u32>(plane) * (bob_plane_bytes / sizeof(amg::u16));
				plane_base[row_word] = source_words[plane];
			}
		}
	}
}

amg::u16* destination_at(amg::u8* planes, amg::u16 x, amg::u16 y) {
	const amg::u32 destination_offset =
		static_cast<amg::u32>(y) * bytes_per_row + (x / 8u);
	return reinterpret_cast<amg::u16*>(planes + destination_offset);
}

/// Posicion horizontal animada del BOB.
///
/// El movimiento se hace en pasos de 16 pixels para no mezclar todavia dos temas:
/// save/restore por Blitter y shifts de Blitter. Cuando esta base este solida,
/// anadiremos X arbitraria con BLTCON shifts y mascara de bordes.
amg::u16 animated_bob_x(amg::u16 frame) {
	const amg::u16 step = static_cast<amg::u16>((frame / 2u) % 10u);
	return static_cast<amg::u16>(bob_start_x + step * 16u);
}

amg::graphics::BlitJob make_copy_job(
	const amg::u16* source,
	amg::u16* destination,
	amg::s16 source_modulo,
	amg::s16 destination_modulo,
	amg::u32 source_stride,
	amg::u32 destination_stride
) {
	return {
		amg::graphics::BlitJobKind::CopyRect,
		nullptr,
		source,
		destination,
		bob_words_per_row,
		bob_height,
		source_modulo,
		destination_modulo,
		plane_count,
		source_stride,
		destination_stride,
	};
}

amg::graphics::BlitJob make_masked_job(
	amg::graphics::BlitJobKind kind,
	const amg::u16* mask,
	const amg::u16* source,
	amg::u16* destination
) {
	return {
		kind,
		mask,
		source,
		destination,
		bob_words_per_row,
		bob_height,
		0,
		static_cast<amg::s16>(bytes_per_row - bob_words_per_row * sizeof(amg::u16)),
		plane_count,
		bob_plane_bytes,
		plane_bytes,
	};
}

struct DemoGame {
	void init(amg::amiga::MinimalBackend& backend, amg::GameContext&) {
		amg::debug::mark_init_started(g_amg_run_status);
		m_memory_ok = backend.configure_memory({
			72u * 1024u,
			8u * 1024u,
			4u * 1024u,
		});

		const drivers::StaticEhbSceneConfig scene_config {
			&palette,
			nullptr,
			0,
			1024,
		};

		m_scene_ok = m_scene.init(backend.memory(), scene_config);
		if (!m_memory_ok || !m_scene_ok) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000050u);
			return;
		}

		add_reference_bars(m_scene.bitplanes());

		m_mask_block = backend.memory().chip.allocate(bob_plane_bytes, 16);
		m_source_block = backend.memory().chip.allocate(bob_plane_bytes * plane_count, 16);
		m_blob_source_block = backend.memory().chip.allocate(bob_plane_bytes * plane_count, 16);
		m_saved_background_block = backend.memory().chip.allocate(bob_plane_bytes * plane_count, 16);
		if (
			!m_mask_block.valid() ||
			!m_source_block.valid() ||
			!m_blob_source_block.valid() ||
			!m_saved_background_block.valid()
		) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000051u);
			return;
		}

		build_masked_bob(
			static_cast<amg::u16*>(m_mask_block.data),
			static_cast<amg::u16*>(m_source_block.data),
			4,
			14
		);
		build_masked_bob(
			static_cast<amg::u16*>(m_mask_block.data),
			static_cast<amg::u16*>(m_blob_source_block.data),
			5,
			7
		);

		m_frame_plan.clear();
		const amg::graphics::BlitJob blob_left_job = make_masked_job(
			amg::graphics::BlitJobKind::MaskedBlobNoSave,
			static_cast<const amg::u16*>(m_mask_block.data),
			static_cast<const amg::u16*>(m_blob_source_block.data),
			destination_at(m_scene.bitplanes(), blob_left_x, blob_y)
		);
		const amg::graphics::BlitJob blob_right_job = make_masked_job(
			amg::graphics::BlitJobKind::MaskedBlobNoSave,
			static_cast<const amg::u16*>(m_mask_block.data),
			static_cast<const amg::u16*>(m_blob_source_block.data),
			destination_at(m_scene.bitplanes(), blob_right_x, blob_y)
		);

		if (
			!m_frame_plan.add_masked_blob_no_save(blob_left_job) ||
			!m_frame_plan.add_masked_blob_no_save(blob_right_job) ||
			!backend.execute_frame_plan(m_frame_plan)
		) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000052u);
			return;
		}

		m_blit_ok = true;
		m_static_no_save_jobs = m_frame_plan.blit_budget().no_save_jobs;
		m_scene.install(backend);
	}

	void update(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		amg::debug::mark_frame(g_amg_run_status, context.frame.frame_index);
		if (!m_blit_ok || !m_scene.ok()) {
			return;
		}

		const amg::u16 current_x = animated_bob_x(context.frame.frame_index);
		amg::u16* current_destination = destination_at(m_scene.bitplanes(), current_x, bob_y);
		amg::u16* saved_background = static_cast<amg::u16*>(m_saved_background_block.data);

		m_frame_plan.clear();

		if (m_has_saved_background) {
			const amg::graphics::BlitJob restore = make_copy_job(
				static_cast<const amg::u16*>(m_saved_background_block.data),
				destination_at(m_scene.bitplanes(), m_previous_x, bob_y),
				0,
				static_cast<amg::s16>(bytes_per_row - bob_words_per_row * sizeof(amg::u16)),
				bob_plane_bytes,
				plane_bytes
			);
			if (!m_frame_plan.add_restore_rect(restore)) {
				amg::debug::mark_failed(g_amg_run_status, 0x00000053u);
				return;
			}
		}

		const amg::graphics::BlitJob save = make_copy_job(
			current_destination,
			saved_background,
			static_cast<amg::s16>(bytes_per_row - bob_words_per_row * sizeof(amg::u16)),
			0,
			plane_bytes,
			bob_plane_bytes
		);
		const amg::graphics::BlitJob draw = make_masked_job(
			amg::graphics::BlitJobKind::MaskedBobCookieCut,
			static_cast<const amg::u16*>(m_mask_block.data),
			static_cast<const amg::u16*>(m_source_block.data),
			current_destination
		);

		if (
			!m_frame_plan.add_copy_rect(save) ||
			!m_frame_plan.add_masked_bob(draw) ||
			!backend.execute_frame_plan(m_frame_plan)
		) {
			amg::debug::mark_failed(g_amg_run_status, 0x00000054u);
			return;
		}

		m_previous_x = current_x;
		m_has_saved_background = true;
		m_last_frame_jobs = m_frame_plan.blit_job_count();
		m_last_frame_words = m_frame_plan.blit_budget().words;

		m_scene.install(backend);

		if (context.frame.frame_index >= 12u) {
			amg::debug::mark_ready(
				g_amg_run_status,
				0x05000000u |
					(static_cast<amg::u32>(m_static_no_save_jobs) << 16u) |
					(static_cast<amg::u32>(m_last_frame_jobs) << 8u) |
					static_cast<amg::u32>(m_last_frame_words / 128u)
			);
		}
	}

	void render(amg::amiga::MinimalBackend& backend, amg::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		amg::debug::probe_when_ready(g_amg_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	bool m_blit_ok = false;
	drivers::StaticEhbScene m_scene {};
	amg::graphics::FramePlan m_frame_plan {};
	amg::MemoryBlock m_mask_block {};
	amg::MemoryBlock m_source_block {};
	amg::MemoryBlock m_blob_source_block {};
	amg::MemoryBlock m_saved_background_block {};
	amg::u16 m_previous_x = bob_start_x;
	amg::u16 m_last_frame_words = 0;
	amg::u8 m_static_no_save_jobs = 0;
	amg::u8 m_last_frame_jobs = 0;
	bool m_has_saved_background = false;
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
