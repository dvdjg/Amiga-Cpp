#include <eng/platform/amiga_minimal.hpp>

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
constexpr unsigned short custom_intena_offset = 0x09a / 2;
constexpr unsigned short custom_intreq_offset = 0x09c / 2;
constexpr unsigned short dma_setclr = 0x8000;
constexpr unsigned short dma_master = 0x0200;
constexpr unsigned short dma_copper = 0x0080;
constexpr unsigned short dma_blitter = 0x0040;
constexpr unsigned short dma_clear_all = 0x7fff;
constexpr unsigned short dmaconr_blitter_busy = 0x4000;
constexpr unsigned short blt_use_a = 0x0800;
constexpr unsigned short blt_use_b = 0x0400;
constexpr unsigned short blt_use_c = 0x0200;
constexpr unsigned short blt_use_d = 0x0100;
constexpr unsigned short blt_minterm_cookie_cut = 0x00ca;
constexpr unsigned short blt_minterm_copy_c = 0x00aa;

void write_custom_pointer(unsigned short word_offset, const void* pointer) {
	const eng::u32 raw = reinterpret_cast<eng::u32>(pointer);
	custom_base[word_offset] = static_cast<eng::u16>(raw >> 16);
	custom_base[word_offset + 1] = static_cast<eng::u16>(raw & 0xffffu);
}

bool wait_blitter() {
	// El bit BBUSY de DMACONR baja cuando el Blitter queda libre. Dejamos un limite
	// alto para evitar bloqueos infinitos durante pruebas si hemos programado mal un
	// registro; en una build de juego esto se convertira en diagnostico/profiler.
	eng::u32 guard = 0x00ffffffu;
	while ((custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u) {
		if (--guard == 0u) {
			return false;
		}
	}
	return true;
}

} // namespace

namespace eng::amiga {

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

	// NOTA: AllocMem de AmigaOS 1.3 solo garantiza alineacion a 8 bytes, no 16.
	// Si el llamador reserva memoria alineada a 16 dentro de una arena (como los
	// planos EHB o copperlists), el puntero base puede tener offset 8-mod-16,
	// generando padding interno en cada allocate() alineado a 16. El tamano total
	// pedido al backend debe incluir +16 bytes de headroom para absorber este
	// padding acumulado. Ver regla en arena.hpp allocate().
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

void MinimalBackend::takeover_display(const u16* copper_words) {
	// Al arrancar, Kickstart/AmigaDOS dejan viva toda la maquina de
	// interrupciones y DMA: exec/graphics/intuition tienen sus handlers
	// de VBL/ports/CIAA armados, y Agnus sigue fetchando el sprite del
	// puntero del Workbench (SPREN activo). Si solo instalamos nuestra
	// copperlist por encima, el sistema sigue "vivo" debajo: handlers de
	// VBL cada frame y un canal de sprite apuntando a datos stale que,
	// cuando se apaga/recarga a media pantalla, deja una barra vertical
	// de un color de paleta (AHRM cap. 4: sprite DMA apagado a mitad de
	// listado -> ultima linea fetchada -> barra vertical). Por eso aqui
	// congelamos TODO antes de arrancar nuestra lista. A partir de este
	// punto el engine NO vuelve a usar exec: el bucle es espera activa
	// por VPOSR y la depuracion usa el canal lateral 0xf0ff60.

	// 1) Apagar interrupciones del sistema y limpiar peticiones.
	custom_base[custom_intena_offset] = dma_clear_all;   // INTENA=0x7FFF
	custom_base[custom_intreq_offset] = dma_clear_all;   // INTREQ=0x7FFF

	// 2) Higiene: esperar un blit que el sistema pudiera tener en vuelo.
	wait_blitter();

	// 3) Apagar TODO el DMA: sprites, disco, audio, blitter, bitplane y
	//    copper. La pantalla queda a COLOR00 un instante, pero nadie lo
	//    ve porque esto esta en el blanking del arranque.
	custom_base[custom_dmacon_offset] = dma_clear_all;   // DMACON=0x7FFF

	// 4) Programar nuestra copperlist (puntero COP1LC como LONG).
	*cop1lc = reinterpret_cast<u32>(copper_words);

	// 5) Esperar el arranque de VBlank (linea 311 -> 0) para que el Copper
	//    arranque ALINEADO al frame y no a media pantalla. Misma espera
	//    activa que wait_vblank() sobre VPOSR (sin interrupciones).
	while ((*vpos_long & 0x1ff00u) == (311u << 8)) {
	}
	while ((*vpos_long & 0x1ff00u) != (311u << 8)) {
	}

	// 6) Arrancar master + copper y forzar el inicio de la lista ya (aun
	//    linea 0-2). Los MOVEs de setup terminan mucho antes de DIWSTRT,
	//    asi que el primer frame sale limpio.
	custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_copper;
	custom_base[custom_copjmp1_offset] = 0x7fff;         // COPJMP1

	m_display_taken = true;
}

void MinimalBackend::install_copper_list(const u16* copper_words) {
	// SWAP de copperlist (doble buffer; la 201 reinstala cada frame).
	// Solo actualizamos el puntero: el Copper recarga COP1LC solo al comienzo
	// del proximo VBlank. NUNCA COPJMP1 aqui: reiniciaria el Copper a media
	// pantalla (ver bug "banda de 1 frame").
	//
	// Retrocompatibilidad: drivers antiguos que solo conocian este metodo
	// (nunca llaman a takeover_display) siguen funcionando: la primera llamada
	// delega en la toma de control completa. Asi los compositores que ya
	// llaman a install_copper_list por frame no necesitan cambios inmediatos.
	if (!m_display_taken) {
		takeover_display(copper_words);
		return;
	}
	*cop1lc = reinterpret_cast<u32>(copper_words);
}

bool MinimalBackend::execute_frame_plan(const graphics::FramePlan& plan) {
	if (!plan.ok()) {
		return false;
	}

	m_blitter_starts = 0;
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
				custom_base[custom_bltcon1_offset] = static_cast<u16>(
					job.descending ? 0x0400 : 0x0000
				);
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
			++m_blitter_starts;
		}
	}

	return wait_blitter();
}

void MinimalBackend::set_warpmode(bool enabled) {
	warpmode(enabled ? 1 : 0);
}

} // namespace eng::amiga
