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
constexpr eng::u16 bob_words_per_row = bob_width / 16;
constexpr eng::u32 bob_plane_bytes = bob_words_per_row * sizeof(eng::u16) * bob_height;
constexpr eng::u16 bob_start_x = 48; // Multiplo de 16: esta fase no usa shifts.
constexpr eng::u16 bob_y = 92;
constexpr eng::u16 blob_left_x = 48;
constexpr eng::u16 blob_right_x = 224;
constexpr eng::u16 blob_y = 150;

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

void fill_screen_with_index(eng::u8* planes, eng::u8 index) {
	for (eng::u8 plane = 0; plane < plane_count; ++plane) {
		const eng::u8 value = (index & (1u << plane)) ? 0xffu : 0x00u;
		eng::u8* plane_base = planes + static_cast<eng::u32>(plane) * plane_bytes;
		for (eng::u32 i = 0; i < plane_bytes; ++i) {
			plane_base[i] = value;
		}
	}
}

void add_reference_bars(eng::u8* planes) {
	// Barras verticales suaves en el fondo. El BOB debe cortarlas limpiamente; si
	// el minterm del Blitter estuviera mal, el analizador detectaria contaminacion
	// de colores o ausencia del blanco central.
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

void build_masked_bob(eng::u16* mask, eng::u16* source, eng::u8 outer_color, eng::u8 inner_color) {
	for (eng::u16 y = 0; y < bob_height; ++y) {
		for (eng::u16 word = 0; word < bob_words_per_row; ++word) {
			const eng::u16 row_word = static_cast<eng::u16>(y * bob_words_per_row + word);
			eng::u16 mask_word = 0;
			eng::u16 source_words[plane_count] {};

			for (eng::u16 bit = 0; bit < 16; ++bit) {
				const eng::s16 x = static_cast<eng::s16>(word * 16u + bit);
				const eng::s16 dx = static_cast<eng::s16>(x - 15);
				const eng::s16 dy = static_cast<eng::s16>(y - 15);
				const bool inside = (dx * dx + dy * dy) <= 230;
				if (!inside) {
					continue;
				}

				const eng::u16 bit_mask = static_cast<eng::u16>(0x8000u >> bit);
				const eng::u8 color_index = (dx * dx + dy * dy < 70) ? inner_color : outer_color;
				mask_word |= bit_mask;
				for (eng::u8 plane = 0; plane < plane_count; ++plane) {
					if (color_index & (1u << plane)) {
						source_words[plane] |= bit_mask;
					}
				}
			}

			mask[row_word] = mask_word;
			for (eng::u8 plane = 0; plane < plane_count; ++plane) {
				eng::u16* plane_base = source + static_cast<eng::u32>(plane) * (bob_plane_bytes / sizeof(eng::u16));
				plane_base[row_word] = source_words[plane];
			}
		}
	}
}

eng::u16* destination_at(eng::u8* planes, eng::u16 x, eng::u16 y) {
	const eng::u32 destination_offset =
		static_cast<eng::u32>(y) * bytes_per_row + (x / 8u);
	return reinterpret_cast<eng::u16*>(planes + destination_offset);
}

/// Posicion horizontal animada del BOB.
///
/// El movimiento se hace en pasos de 16 pixels para no mezclar todavia dos temas:
/// save/restore por Blitter y shifts de Blitter. Cuando esta base este solida,
/// anadiremos X arbitraria con BLTCON shifts y mascara de bordes.
eng::u16 animated_bob_x(eng::u16 frame) {
	const eng::u16 step = static_cast<eng::u16>((frame / 2u) % 10u);
	return static_cast<eng::u16>(bob_start_x + step * 16u);
}

eng::graphics::DirtyRect bob_dirty_rect(eng::u16 x, eng::u16 y) {
	return {
		static_cast<eng::s16>(x),
		static_cast<eng::s16>(y),
		static_cast<eng::s16>(x + bob_width),
		static_cast<eng::s16>(y + bob_height),
	};
}

void configure_blit_budget(eng::graphics::FramePlan& plan) {
	// La demo 050 es pequena a proposito: dos blobs fijos y un BOB animado con
	// restore/save/draw. Estos limites convierten esa intencion en contrato
	// verificable sin pretender todavia medir ciclos exactos del bus.
	plan.set_blit_budget_limits({
		1300,
		1600,
		4,
		6,
	});
}

eng::graphics::BlitJob make_copy_job(
	const eng::u16* source,
	eng::u16* destination,
	eng::s16 source_modulo,
	eng::s16 destination_modulo,
	eng::u32 source_stride,
	eng::u32 destination_stride
) {
	return {
		eng::graphics::BlitJobKind::CopyRect,
		nullptr,
		source,
		destination,
		bob_words_per_row,
		bob_height,
		source_modulo,
		destination_modulo,
		plane_count,
		0,
		source_stride,
		destination_stride,
	};
}

eng::graphics::BlitJob make_masked_job(
	eng::graphics::BlitJobKind kind,
	const eng::u16* mask,
	const eng::u16* source,
	eng::u16* destination
) {
	return {
		kind,
		mask,
		source,
		destination,
		bob_words_per_row,
		bob_height,
		0,
		static_cast<eng::s16>(bytes_per_row - bob_words_per_row * sizeof(eng::u16)),
		plane_count,
		0,
		bob_plane_bytes,
		plane_bytes,
	};
}

struct DemoGame {
	void init(eng::amiga::MinimalBackend& backend, eng::GameContext&) {
		eng::debug::mark_init_started(g_eng_run_status);
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
			eng::debug::mark_failed(g_eng_run_status, 0x00000050u);
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
			eng::debug::mark_failed(g_eng_run_status, 0x00000051u);
			return;
		}

		build_masked_bob(
			static_cast<eng::u16*>(m_mask_block.data),
			static_cast<eng::u16*>(m_source_block.data),
			4,
			14
		);
		build_masked_bob(
			static_cast<eng::u16*>(m_mask_block.data),
			static_cast<eng::u16*>(m_blob_source_block.data),
			5,
			7
		);

		m_frame_plan.clear();
		configure_blit_budget(m_frame_plan);
		m_frame_plan.add_dirty_rect(bob_dirty_rect(blob_left_x, blob_y));
		m_frame_plan.add_dirty_rect(bob_dirty_rect(blob_right_x, blob_y));
		const eng::graphics::BlitJob blob_left_job = make_masked_job(
			eng::graphics::BlitJobKind::MaskedBlobNoSave,
			static_cast<const eng::u16*>(m_mask_block.data),
			static_cast<const eng::u16*>(m_blob_source_block.data),
			destination_at(m_scene.bitplanes(), blob_left_x, blob_y)
		);
		const eng::graphics::BlitJob blob_right_job = make_masked_job(
			eng::graphics::BlitJobKind::MaskedBlobNoSave,
			static_cast<const eng::u16*>(m_mask_block.data),
			static_cast<const eng::u16*>(m_blob_source_block.data),
			destination_at(m_scene.bitplanes(), blob_right_x, blob_y)
		);

		if (
			!m_frame_plan.add_masked_blob_no_save(blob_left_job) ||
			!m_frame_plan.add_masked_blob_no_save(blob_right_job) ||
			m_frame_plan.blit_budget_report().status == eng::graphics::BlitBudgetStatus::Exceeded ||
			!backend.execute_frame_plan(m_frame_plan)
		) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000052u);
			return;
		}

		m_blit_ok = true;
		m_static_no_save_jobs = m_frame_plan.blit_budget().no_save_jobs;
		m_scene.install(backend);
	}

	void update(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		eng::debug::mark_frame(g_eng_run_status, context.frame.frame_index);
		if (!m_blit_ok || !m_scene.ok()) {
			return;
		}
		// El runner captura la pantalla desde el canal lateral mientras el 68000
		// podria seguir ejecutando. Al congelar la demo tras el frame validado,
		// evitamos capturas en mitad de restore/save/draw y convertimos la prueba
		// visual en una evidencia determinista del resultado final.
		if (m_validation_frame_ready) {
			return;
		}

		const eng::u16 current_x = animated_bob_x(context.frame.frame_index);
		eng::u16* current_destination = destination_at(m_scene.bitplanes(), current_x, bob_y);
		eng::u16* saved_background = static_cast<eng::u16*>(m_saved_background_block.data);

		m_frame_plan.clear();
		configure_blit_budget(m_frame_plan);

		if (m_has_saved_background) {
			if (!m_frame_plan.add_dirty_rect(bob_dirty_rect(m_previous_x, bob_y))) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000055u);
				return;
			}
			const eng::graphics::BlitJob restore = make_copy_job(
				static_cast<const eng::u16*>(m_saved_background_block.data),
				destination_at(m_scene.bitplanes(), m_previous_x, bob_y),
				0,
				static_cast<eng::s16>(bytes_per_row - bob_words_per_row * sizeof(eng::u16)),
				bob_plane_bytes,
				plane_bytes
			);
			if (!m_frame_plan.add_restore_rect(restore)) {
				eng::debug::mark_failed(g_eng_run_status, 0x00000053u);
				return;
			}
		}

		if (!m_frame_plan.add_dirty_rect(bob_dirty_rect(current_x, bob_y))) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000056u);
			return;
		}

		const eng::graphics::BlitJob save = make_copy_job(
			current_destination,
			saved_background,
			static_cast<eng::s16>(bytes_per_row - bob_words_per_row * sizeof(eng::u16)),
			0,
			plane_bytes,
			bob_plane_bytes
		);
		const eng::graphics::BlitJob draw = make_masked_job(
			eng::graphics::BlitJobKind::MaskedBobCookieCut,
			static_cast<const eng::u16*>(m_mask_block.data),
			static_cast<const eng::u16*>(m_source_block.data),
			current_destination
		);

		if (
			!m_frame_plan.add_copy_rect(save) ||
			!m_frame_plan.add_masked_bob(draw) ||
			m_frame_plan.blit_budget_report().status == eng::graphics::BlitBudgetStatus::Exceeded ||
			!backend.execute_frame_plan(m_frame_plan)
		) {
			eng::debug::mark_failed(g_eng_run_status, 0x00000054u);
			return;
		}

		m_previous_x = current_x;
		m_has_saved_background = true;
		m_last_frame_jobs = m_frame_plan.blit_job_count();
		m_last_frame_words = m_frame_plan.blit_budget().words;
		m_last_dirty_rects = m_frame_plan.dirty_report().rects;
		m_last_dirty_merges = m_frame_plan.dirty_report().merges;

		m_scene.install(backend);

		if (context.frame.frame_index >= 12u) {
			eng::debug::mark_ready(
				g_eng_run_status,
				0x05000000u |
					(static_cast<eng::u32>(m_static_no_save_jobs) << 16u) |
					(static_cast<eng::u32>(m_last_frame_jobs) << 8u) |
					(static_cast<eng::u32>(m_last_dirty_rects) << 4u) |
					static_cast<eng::u32>(m_last_dirty_merges & 0x0fu)
			);
			m_validation_frame_ready = true;
		}
	}

	void render(eng::amiga::MinimalBackend& backend, eng::GameContext& context) {
		if (m_scene.ok()) {
			m_scene.install(backend);
		}
		eng::debug::probe_when_ready(g_eng_run_status, context.frame.frame_index);
	}

	bool m_memory_ok = false;
	bool m_scene_ok = false;
	bool m_blit_ok = false;
	drivers::StaticEhbScene m_scene {};
	eng::graphics::FramePlan m_frame_plan {};
	eng::MemoryBlock m_mask_block {};
	eng::MemoryBlock m_source_block {};
	eng::MemoryBlock m_blob_source_block {};
	eng::MemoryBlock m_saved_background_block {};
	eng::u16 m_previous_x = bob_start_x;
	eng::u16 m_last_frame_words = 0;
	eng::u8 m_static_no_save_jobs = 0;
	eng::u8 m_last_frame_jobs = 0;
	eng::u8 m_last_dirty_rects = 0;
	eng::u8 m_last_dirty_merges = 0;
	bool m_has_saved_background = false;
	bool m_validation_frame_ready = false;
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
