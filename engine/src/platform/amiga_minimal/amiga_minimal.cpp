#include <amg/platform/amiga_minimal.hpp>

#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <exec/memory.h>

namespace {

volatile unsigned short* const custom_base = reinterpret_cast<volatile unsigned short*>(0xdff000);
volatile unsigned long* const vpos_long = reinterpret_cast<volatile unsigned long*>(0xdff004);
volatile unsigned long* const cop1lc = reinterpret_cast<volatile unsigned long*>(0xdff080);

constexpr unsigned short custom_dmaconr_offset = 0x002 / 2;
constexpr unsigned short custom_bltcon0_offset = 0x040 / 2;
constexpr unsigned short custom_bltcon1_offset = 0x042 / 2;
constexpr unsigned short custom_bltafwm_offset = 0x044 / 2;
constexpr unsigned short custom_bltalwm_offset = 0x046 / 2;
constexpr unsigned short custom_bltcpt_offset = 0x048 / 2;
constexpr unsigned short custom_bltbpt_offset = 0x04c / 2;
constexpr unsigned short custom_bltapt_offset = 0x050 / 2;
constexpr unsigned short custom_bltdpt_offset = 0x054 / 2;
constexpr unsigned short custom_bltsize_offset = 0x058 / 2;
constexpr unsigned short custom_bltcmod_offset = 0x060 / 2;
constexpr unsigned short custom_bltbmod_offset = 0x062 / 2;
constexpr unsigned short custom_bltamod_offset = 0x064 / 2;
constexpr unsigned short custom_bltdmod_offset = 0x066 / 2;
constexpr unsigned short custom_color_offset = 0x180 / 2;
constexpr unsigned short custom_copjmp1_offset = 0x088 / 2;
constexpr unsigned short custom_dmacon_offset = 0x096 / 2;
constexpr unsigned short dma_setclr = 0x8000;
constexpr unsigned short dma_master = 0x0200;
constexpr unsigned short dma_copper = 0x0080;
constexpr unsigned short dma_blitter = 0x0040;
constexpr unsigned short dmaconr_blitter_busy = 0x4000;
constexpr unsigned short blt_use_a = 0x0800;
constexpr unsigned short blt_use_b = 0x0400;
constexpr unsigned short blt_use_c = 0x0200;
constexpr unsigned short blt_use_d = 0x0100;
constexpr unsigned short blt_minterm_cookie_cut = 0x00ca;
constexpr unsigned short blt_minterm_copy_c = 0x00aa;

void write_custom_pointer(unsigned short word_offset, const void* pointer) {
	const amg::u32 raw = reinterpret_cast<amg::u32>(pointer);
	custom_base[word_offset] = static_cast<amg::u16>(raw >> 16);
	custom_base[word_offset + 1] = static_cast<amg::u16>(raw & 0xffffu);
}

bool wait_blitter() {
	// El bit BBUSY de DMACONR baja cuando el Blitter queda libre. Dejamos un limite
	// alto para evitar bloqueos infinitos durante pruebas si hemos programado mal un
	// registro; en una build de juego esto se convertira en diagnostico/profiler.
	amg::u32 guard = 0x00ffffffu;
	while ((custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u) {
		if (--guard == 0u) {
			return false;
		}
	}
	return true;
}

} // namespace

namespace amg::amiga {

void DebugOverlay::clear() {
	debug_clear();
}

void DebugOverlay::text(s16 x, s16 y, const char* value, u32 rgb) {
	debug_text(x, y, value, rgb);
}

void DebugOverlay::rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb) {
	debug_rect(left, top, right, bottom, rgb);
}

void DebugOverlay::filled_rect(s16 left, s16 top, s16 right, s16 bottom, u32 rgb) {
	debug_filled_rect(left, top, right, bottom, rgb);
}

MinimalBackend::~MinimalBackend() {
	release_memory();
}

void MinimalBackend::boot() {
	set_warpmode(false);
}

bool MinimalBackend::configure_memory(const MemoryConfig& config) {
	release_memory();

	if (config.chip_bytes != 0) {
		m_chip_alloc = AllocMem(config.chip_bytes, MEMF_CHIP | MEMF_CLEAR);
		m_chip_alloc_size = m_chip_alloc ? config.chip_bytes : 0;
	}

	if (config.slow_bytes != 0) {
		// On an A500 trapdoor expansion AmigaOS exposes this as non-chip memory.
		// It is still "Slow" from the engine perspective because it is not true
		// CPU-private Fast RAM.
		m_slow_alloc = AllocMem(config.slow_bytes, MEMF_FAST | MEMF_CLEAR);
		if (!m_slow_alloc) {
			m_slow_alloc = AllocMem(config.slow_bytes, MEMF_ANY | MEMF_CLEAR);
		}
		m_slow_alloc_size = m_slow_alloc ? config.slow_bytes : 0;
	}

	if (config.frame_bytes != 0) {
		m_frame_alloc = AllocMem(config.frame_bytes, MEMF_CHIP | MEMF_CLEAR);
		m_frame_alloc_size = m_frame_alloc ? config.frame_bytes : 0;
	}

	m_memory.chip.reset(m_chip_alloc, m_chip_alloc_size, MemoryKind::Chip);
	m_memory.slow.reset(m_slow_alloc, m_slow_alloc_size, MemoryKind::Slow);
	m_memory.frame.reset(m_frame_alloc, m_frame_alloc_size, MemoryKind::Chip);

	m_memory_report.chip = m_memory.chip.snapshot();
	m_memory_report.slow = m_memory.slow.snapshot();
	m_memory_report.frame = m_memory.frame.snapshot();
	m_memory_report.chip_ok = config.chip_bytes == 0 || m_chip_alloc != nullptr;
	m_memory_report.slow_ok = config.slow_bytes == 0 || m_slow_alloc != nullptr;
	m_memory_report.frame_ok = config.frame_bytes == 0 || m_frame_alloc != nullptr;

	return m_memory_report.ok();
}

void MinimalBackend::release_memory() {
	if (m_frame_alloc) {
		FreeMem(m_frame_alloc, m_frame_alloc_size);
		m_frame_alloc = nullptr;
		m_frame_alloc_size = 0;
	}

	if (m_slow_alloc) {
		FreeMem(m_slow_alloc, m_slow_alloc_size);
		m_slow_alloc = nullptr;
		m_slow_alloc_size = 0;
	}

	if (m_chip_alloc) {
		FreeMem(m_chip_alloc, m_chip_alloc_size);
		m_chip_alloc = nullptr;
		m_chip_alloc_size = 0;
	}

	m_memory = {};
	m_memory_report = {};
}

void MinimalBackend::wait_vblank() {
	debug_start_idle();
	while ((*vpos_long & 0x1ff00u) == (311u << 8)) {
	}
	while ((*vpos_long & 0x1ff00u) != (311u << 8)) {
	}
	debug_stop_idle();
}

void MinimalBackend::set_color(u8 index, u16 rgb444) {
	if (index < 32) {
		custom_base[custom_color_offset + index] = rgb444;
	}
}

void MinimalBackend::install_copper_list(const u16* copper_words) {
	// COP1LC is a 32-bit pointer split over two custom registers. Writing it as a
	// long mirrors the classic examples and is safe on 68000-aligned addresses.
	*cop1lc = reinterpret_cast<u32>(copper_words);

	// COPJMP1 forces the Copper to reload COP1LC immediately.
	custom_base[custom_copjmp1_offset] = 0x7fff;

	// Enable master DMA and Copper DMA. We leave bitplane DMA to the copperlist or
	// future display driver; this demo starts with zero bitplanes.
	custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_copper;
}

bool MinimalBackend::execute_frame_plan(const graphics::FramePlan& plan) {
	if (!plan.ok()) {
		return false;
	}

	for (u8 job_index = 0; job_index < plan.blit_job_count(); ++job_index) {
		const graphics::BlitJob& job = plan.blit_job(job_index);
		const bool masked =
			job.kind == graphics::BlitJobKind::MaskedBobCookieCut ||
			job.kind == graphics::BlitJobKind::MaskedBlobNoSave;
		const bool copy =
			job.kind == graphics::BlitJobKind::CopyRect ||
			job.kind == graphics::BlitJobKind::RestoreRect ||
			job.kind == graphics::BlitJobKind::TileBlockCopy;

		if (!masked && !copy) {
			return false;
		}

		custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_blitter;

		const u32 source_plane_stride_words = job.source_plane_stride_bytes / sizeof(u16);
		const u32 destination_plane_stride_words = job.destination_plane_stride_bytes / sizeof(u16);
		for (u8 plane = 0; plane < job.bitplane_count; ++plane) {
			if (!wait_blitter()) {
				return false;
			}

			const u16* source_plane = job.source + static_cast<u32>(plane) * source_plane_stride_words;
			u16* destination_plane = job.destination + static_cast<u32>(plane) * destination_plane_stride_words;

			if (masked) {
				custom_base[custom_bltcon0_offset] = static_cast<u16>(
					(static_cast<u16>(job.source_shift) << 12u) |
					blt_use_a | blt_use_b | blt_use_c | blt_use_d | blt_minterm_cookie_cut
				);
				custom_base[custom_bltcon1_offset] = static_cast<u16>(
					static_cast<u16>(job.source_shift) << 12u
				);
			} else {
				custom_base[custom_bltcon0_offset] = static_cast<u16>(
					blt_use_c | blt_use_d | blt_minterm_copy_c
				);
				custom_base[custom_bltcon1_offset] = 0x0000;
			}
			custom_base[custom_bltafwm_offset] = 0xffff;
			custom_base[custom_bltalwm_offset] = 0xffff;
			custom_base[custom_bltamod_offset] = static_cast<u16>(masked ? job.source_modulo_bytes : 0);
			custom_base[custom_bltbmod_offset] = static_cast<u16>(masked ? job.source_modulo_bytes : 0);
			custom_base[custom_bltcmod_offset] = static_cast<u16>(masked ? job.destination_modulo_bytes : job.source_modulo_bytes);
			custom_base[custom_bltdmod_offset] = static_cast<u16>(job.destination_modulo_bytes);

			if (masked) {
				write_custom_pointer(custom_bltapt_offset, job.mask);
				write_custom_pointer(custom_bltbpt_offset, source_plane);
				write_custom_pointer(custom_bltcpt_offset, destination_plane);
			} else {
				write_custom_pointer(custom_bltcpt_offset, source_plane);
			}
			write_custom_pointer(custom_bltdpt_offset, destination_plane);

			custom_base[custom_bltsize_offset] = static_cast<u16>(
				(static_cast<u16>(job.height) << 6) | job.words_per_row
			);
		}
	}

	return wait_blitter();
}

void MinimalBackend::set_warpmode(bool enabled) {
	warpmode(enabled ? 1 : 0);
}

} // namespace amg::amiga
