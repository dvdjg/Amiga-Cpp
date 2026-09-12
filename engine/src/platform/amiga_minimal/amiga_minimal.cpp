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
constexpr unsigned short custom_bltadat_offset = 0x074 / 2; // BLTADAT
constexpr unsigned short custom_bltbdat_offset = 0x072 / 2; // BLTBDAT
constexpr unsigned short custom_bltcdat_offset = 0x070 / 2; // BLTCDAT
constexpr unsigned short custom_bpldat_offset = 0x110 / 2;  // BPL1DAT (+2 por plano)
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

// Servicio opcional que se ejecuta mientras se espera al Blitter (drenado de fondo).
void (*g_blitter_service)(void*, unsigned short) = nullptr;
void* g_blitter_service_user = nullptr;

bool wait_blitter() {
	// El bit BBUSY de DMACONR baja cuando el Blitter queda libre. Dejamos un limite
	// alto para evitar bloqueos infinitos durante pruebas si hemos programado mal un
	// registro; en una build de juego esto se convertira en diagnostico/profiler.
	// Mientras gira, si hay un servicio de fondo registrado, lo ejecuta (no perder el
	// tiempo de espera en trabajo util).
	eng::u32 guard = 0x00ffffffu;
	while ((custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u) {
		if (g_blitter_service != nullptr) {
			g_blitter_service(g_blitter_service_user,
					  static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8));
		}
		if (--guard == 0u) {
			return false;
		}
	}
	return true;
}

// --- Blitter: linea (line mode) y relleno de area (area fill) ------------------
//
// Portado de la API `libblit` del demoscene (`BlitterLine.c`, `BlitterFillArea.c`)
// y de `amiga-bootcamp/08_graphics/blitter_programming.md` (seccion "Area Fill").
// Tecnica para poligonos convexos:
//   1) dibujar el contorno con line mode (minterm XOR, SING=ONEDOT);
//   2) rellenar con area fill inclusivo (BLTCON1 = DESC|FILL_OR), descendente.
// El relleno funciona bit a bit por plano; para colorear 3 planos con painter se
// usa un plano-mascara 1 bit y luego un cookie-cut por plano (ver fill_triangles).
constexpr unsigned short blt_line_or = 0x0bca;    // BC0F_LINE_OR (contorno/línea)
constexpr unsigned short blt_linemode = 0x0001;
constexpr unsigned short blt_onedot = 0x0002;
constexpr unsigned short blt_sud = 0x0010;
constexpr unsigned short blt_sul = 0x0008;
constexpr unsigned short blt_aul = 0x0004;
constexpr unsigned short blt_signflag = 0x0040;
constexpr unsigned short blt_fill_or = 0x0008;
constexpr unsigned short blt_reverse = 0x0002;
constexpr unsigned short blt_minterm_a_or_c = 0x00fa;       // D = A | C
constexpr unsigned short blt_minterm_not_a_and_c = 0x000a;  // D = ~A & C
// C2P 4bpp (portado de fire-rgb): interleave de bytes (A>>8 | B&~0xFF) y su inverso.
constexpr unsigned short blt_c2p_abd = static_cast<unsigned short>(blt_use_a | blt_use_b | blt_use_d);
constexpr unsigned short blt_minterm_c2p_out = 0x00e4;  // (A&C)|(B&~C) = ABC|ANBC|ABNC|NABNC
constexpr unsigned short blt_minterm_c2p_out2 = 0x00d8; // (A&~C)|(B&C) = ABNC|ANBNC|ABC|NABC
constexpr unsigned short blt_shift8 = 0x8000;           // ASHIFT(8)
constexpr unsigned short blt_shift4 = 0x4000;           // ASHIFT(4)

unsigned short ror16(unsigned short value, unsigned short n) {
	return static_cast<unsigned short>((value >> n) | (value << (16u - n)));
}

/// Borra una region de palabras (D=0) en un plano planar.
void blit_clear_region(eng::u8* plane, eng::u16 row_bytes, eng::u16 wx0, eng::s16 y,
		       eng::u16 words, eng::u16 h) {
	eng::u8* d = plane + static_cast<eng::u32>(y) * row_bytes + (wx0 >> 3);
	const eng::u16 mod = static_cast<eng::u16>(row_bytes - words * 2u);
	wait_blitter();
	custom_base[custom_bltcon0_offset] = blt_use_d; // minterm 0 => D = 0
	custom_base[custom_bltcon1_offset] = 0;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltdmod_offset] = mod;
	write_custom_pointer(custom_bltdpt_offset, d);
	custom_base[custom_bltsize_offset] = static_cast<eng::u16>((h << 6) | words);
}

/// Dibuja una linea con el Blitter (Bresenham hardware, line mode ONEDOT).
void blit_line(eng::u8* plane, eng::u16 row_bytes, eng::s16 x1, eng::s16 y1,
	       eng::s16 x2, eng::s16 y2) {
	if (y1 > y2) {
		eng::s16 t = x1; x1 = x2; x2 = t;
		t = y1; y1 = y2; y2 = t;
	}
	eng::u8* data = plane + static_cast<eng::u32>(y1) * row_bytes + ((x1 >> 3) & ~1);
	eng::s16 dx = static_cast<eng::s16>(x2 - x1);
	eng::s16 dy = static_cast<eng::s16>(y2 - y1);
	eng::u16 con1 = static_cast<eng::u16>(blt_linemode | blt_onedot);
	if (dx < 0) {
		dx = static_cast<eng::s16>(-dx);
		if (dx >= dy) {
			con1 = static_cast<eng::u16>(con1 | blt_aul | blt_sud);
		} else {
			con1 = static_cast<eng::u16>(con1 | blt_sul);
			const eng::s16 t = dx; dx = dy; dy = t;
		}
	} else {
		if (dx >= dy) {
			con1 = static_cast<eng::u16>(con1 | blt_sud);
		} else {
			const eng::s16 t = dx; dx = dy; dy = t;
		}
	}
	eng::s16 derr = static_cast<eng::s16>(dy + dy - dx);
	if (derr < 0) {
		con1 = static_cast<eng::u16>(con1 | blt_signflag);
	}
	const eng::u16 con0 = static_cast<eng::u16>(ror16(static_cast<eng::u16>(x1 & 15), 4) | blt_line_or);
	const eng::u16 amod = static_cast<eng::u16>(derr - dx);
	const eng::u16 bmod = static_cast<eng::u16>(dy + dy);
	wait_blitter();
	custom_base[custom_bltcon0_offset] = con0;
	custom_base[custom_bltcon1_offset] = con1;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltadat_offset] = 0x8000;
	custom_base[custom_bltbdat_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = amod;
	custom_base[custom_bltbmod_offset] = bmod;
	custom_base[custom_bltcmod_offset] = row_bytes;
	custom_base[custom_bltdmod_offset] = row_bytes;
	write_custom_pointer(custom_bltapt_offset,
			     reinterpret_cast<void*>(static_cast<eng::u32>(static_cast<eng::s32>(derr))));
	write_custom_pointer(custom_bltcpt_offset, data);
	write_custom_pointer(custom_bltdpt_offset, data);
	custom_base[custom_bltsize_offset] = static_cast<eng::u16>((static_cast<eng::u16>(dx) << 6) + 66u);
}

/// Rellena (area fill inclusivo) el interior del contorno ya dibujado en `plane`.
/// Descendente y bit a bit: requiere que el contorno sea de 1 pixel (ONEDOT).
void blit_fill_region(eng::u8* plane, eng::u16 row_bytes, eng::u16 wx0, eng::s16 y,
		      eng::u16 words, eng::u16 h) {
	// Relleno ASCENDENTE desde la primera palabra de la region.
	eng::u8* first = plane + static_cast<eng::u32>(y) * row_bytes + (wx0 >> 3);
	const eng::u16 mod = static_cast<eng::u16>(row_bytes - words * 2u);
	wait_blitter();
	custom_base[custom_bltcon0_offset] = static_cast<eng::u16>(blt_use_a | blt_use_d | 0x00f0); // A_TO_D
	custom_base[custom_bltcon1_offset] = blt_fill_or;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = mod;
	custom_base[custom_bltdmod_offset] = mod;
	write_custom_pointer(custom_bltapt_offset, first);
	write_custom_pointer(custom_bltdpt_offset, first);
	custom_base[custom_bltsize_offset] = static_cast<eng::u16>((h << 6) | words);
}

/// Cookie-cut de la mascara 1 bit a un plano de color: `set` -> D = A | D (pone a
/// 1 donde la mascara), `!set` -> D = ~A & D (borra donde la mascara).
void blit_mask_to_plane(eng::u8* dst, eng::u16 row_bytes, const eng::u8* mask,
			eng::u16 wx0, eng::s16 y, eng::u16 words, eng::u16 h, bool set) {
	eng::u8* d = dst + static_cast<eng::u32>(y) * row_bytes + (wx0 >> 3);
	const eng::u8* m = mask + static_cast<eng::u32>(y) * row_bytes + (wx0 >> 3);
	const eng::u16 mod = static_cast<eng::u16>(row_bytes - words * 2u);
	wait_blitter();
	custom_base[custom_bltcon0_offset] = static_cast<eng::u16>(
		blt_use_a | blt_use_c | blt_use_d | (set ? blt_minterm_a_or_c : blt_minterm_not_a_and_c));
	custom_base[custom_bltcon1_offset] = 0;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = mod;
	custom_base[custom_bltcmod_offset] = mod;
	custom_base[custom_bltdmod_offset] = mod;
	write_custom_pointer(custom_bltapt_offset, m);
	write_custom_pointer(custom_bltcpt_offset, d);
	write_custom_pointer(custom_bltdpt_offset, d);
	custom_base[custom_bltsize_offset] = static_cast<eng::u16>((h << 6) | words);
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

void MinimalBackend::wait_vblank(void (*task)(void*, u16), void* user) {
	debug_start_idle();
	// Una sola lectura de VPOSR por iteracion: la condicion y el `vpos` que recibe
	// la tarea comparten el mismo valor (no anade accesos al bus en el bucle caliente).
	u32 vposr = *vpos_long;
	while ((vposr & 0x1ff00u) == (311u << 8)) {
		if (task != nullptr) task(user, static_cast<u16>((vposr & 0x1ff00u) >> 8));
		vposr = *vpos_long;
	}
	while ((vposr & 0x1ff00u) != (311u << 8)) {
		if (task != nullptr) task(user, static_cast<u16>((vposr & 0x1ff00u) >> 8));
		vposr = *vpos_long;
	}
	debug_stop_idle();
}

void MinimalBackend::set_blitter_service(void (*task)(void*, u16), void* user) {
	g_blitter_service = task;
	g_blitter_service_user = user;
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

bool MinimalBackend::fill_triangles_blitter(const FlatTriangle* tris, u32 count,
					    u8* dst, u8 planes, u16 row_bytes, u32 plane_bytes,
					    u8* mask) {
	if (tris == nullptr || dst == nullptr || mask == nullptr || planes == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);

	for (u32 i = 0; i < count; ++i) {
		const FlatTriangle& t = tris[i];
		s16 xmin = t.x0, xmax = t.x0, ymin = t.y0, ymax = t.y0;
		if (t.x1 < xmin) xmin = t.x1;
		if (t.x1 > xmax) xmax = t.x1;
		if (t.x2 < xmin) xmin = t.x2;
		if (t.x2 > xmax) xmax = t.x2;
		if (t.y1 < ymin) ymin = t.y1;
		if (t.y1 > ymax) ymax = t.y1;
		if (t.y2 < ymin) ymin = t.y2;
		if (t.y2 > ymax) ymax = t.y2;
		if (xmax < 0 || ymax < 0 || xmin > 319 || ymin > 255) {
			continue;
		}
		if (xmin < 0) xmin = 0;
		if (ymin < 0) ymin = 0;
		if (xmax > 319) xmax = 319;
		if (ymax > 255) ymax = 255;

		const u16 wx0 = static_cast<u16>(xmin) & 0xfff0u;
		const u16 wx1 = static_cast<u16>(xmax) | 0x000fu;
		const u16 words = static_cast<u16>((static_cast<u16>(wx1 - wx0) + 16u) >> 4);
		const u16 h = static_cast<u16>(ymax - ymin + 1);

		// 1) mascara limpia, 2) contorno, 3) area fill, 4) cookie-cut a color.
		blit_clear_region(mask, row_bytes, wx0, ymin, words, h);
		blit_line(mask, row_bytes, t.x0, t.y0, t.x1, t.y1);
		blit_line(mask, row_bytes, t.x1, t.y1, t.x2, t.y2);
		blit_line(mask, row_bytes, t.x2, t.y2, t.x0, t.y0);
		blit_fill_region(mask, row_bytes, wx0, ymin, words, h);
		for (u8 p = 0; p < planes; ++p) {
			blit_mask_to_plane(dst + static_cast<u32>(p) * plane_bytes, row_bytes, mask,
					   wx0, ymin, words, h, ((t.color >> p) & 1u) != 0u);
		}
	}
	return wait_blitter();
}

bool MinimalBackend::blit_fill_from_mask(const u8* mask, u8* dst, u8 planes, u16 row_bytes,
					 u32 plane_bytes, s16 x, s16 y, u16 w, u16 h, u8 color) {
	if (mask == nullptr || dst == nullptr || planes == 0u || w == 0u || h == 0u) {
		return false;
	}
	s16 x0 = x, y0 = y;
	s16 x1 = static_cast<s16>(x + static_cast<s16>(w) - 1);
	s16 y1 = static_cast<s16>(y + static_cast<s16>(h) - 1);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > 319) x1 = 319;
	if (y1 > 255) y1 = 255;
	if (x0 > x1 || y0 > y1) {
		return true;
	}
	const u16 wx0 = static_cast<u16>(x0) & 0xfff0u;
	const u16 wx1 = static_cast<u16>(x1) | 0x000fu;
	const u16 words = static_cast<u16>((static_cast<u16>(wx1 - wx0) + 16u) >> 4);
	const u16 hh = static_cast<u16>(y1 - y0 + 1);

	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	for (u8 p = 0; p < planes; ++p) {
		blit_mask_to_plane(dst + static_cast<u32>(p) * plane_bytes, row_bytes, mask,
				   wx0, y0, words, hh, ((color >> p) & 1u) != 0u);
	}
	return wait_blitter();
}

bool MinimalBackend::blitter_line(u8* plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1) {
	if (plane == nullptr) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);

	wait_blitter();
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltadat_offset] = 0x8000;
	custom_base[custom_bltbdat_offset] = 0xffff;
	custom_base[custom_bltcmod_offset] = row_bytes;
	custom_base[custom_bltdmod_offset] = row_bytes;

	if (y0 > y1) {
		s16 t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}

	s16 dmax = static_cast<s16>(x1 - x0);
	s16 dmin = static_cast<s16>(y1 - y0);
	u16 bltcon1 = blt_linemode;
	if (dmax < 0) {
		dmax = static_cast<s16>(-dmax);
	}
	if (dmax >= dmin) {
		bltcon1 = static_cast<u16>(bltcon1 | (x0 >= x1 ? (blt_aul | blt_sud) : blt_sud));
	} else {
		if (x0 >= x1) {
			bltcon1 = static_cast<u16>(bltcon1 | blt_sul);
		}
		const s16 t = dmax; dmax = dmin; dmin = t;
	}

	u8* data = plane + static_cast<u32>(y0) * row_bytes + (static_cast<u32>(x0) >> 3);
	data = reinterpret_cast<u8*>(reinterpret_cast<u32>(data) & ~1u);

	dmin = static_cast<s16>(dmin << 1);
	s16 derr = static_cast<s16>(dmin - dmax);
	if (derr < 0) {
		bltcon1 = static_cast<u16>(bltcon1 | blt_signflag);
	}
	bltcon1 = static_cast<u16>(bltcon1 | ror16(static_cast<u16>(x0 & 15), 4));
	const u16 bltcon0 = static_cast<u16>(ror16(static_cast<u16>(x0 & 15), 4) | blt_line_or);
	const u16 bltamod = static_cast<u16>(derr - dmax);
	const u16 bltbmod = static_cast<u16>(dmin);
	const u16 bltsize = static_cast<u16>((static_cast<u16>(dmax) << 6) + 66u);

	wait_blitter();
	custom_base[custom_bltcon0_offset] = bltcon0;
	custom_base[custom_bltcon1_offset] = bltcon1;
	custom_base[custom_bltamod_offset] = bltamod;
	custom_base[custom_bltbmod_offset] = bltbmod;
	write_custom_pointer(custom_bltapt_offset,
			     reinterpret_cast<void*>(static_cast<u32>(static_cast<s32>(derr))));
	write_custom_pointer(custom_bltcpt_offset, data);
	write_custom_pointer(custom_bltdpt_offset, data);
	custom_base[custom_bltsize_offset] = bltsize;
	return wait_blitter();
}

bool MinimalBackend::blitter_clear(u8* dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h) {
	if (dst == nullptr || planes == 0u || w < 16u || h == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	const u16 words = static_cast<u16>(w / 16u);
	for (u8 p = 0; p < planes; ++p) {
		blit_clear_region(dst + static_cast<u32>(p) * plane_bytes, row_bytes, 0, 0, words, h);
	}
	return wait_blitter();
}

void MinimalBackend::set_bitplane_dat(u8 plane, u16 value) {
	if (plane < 8u) {
		custom_base[custom_bpldat_offset + plane] = value;
	}
}

bool MinimalBackend::c2p_4bpp_program(C2p4State& s) {
	if (s.chunky == nullptr) {
		return false;
	}
	u8* src = s.chunky;
	u8* dst = s.chunky + s.bytes;
	const u16 h = static_cast<u16>((static_cast<u32>(s.bytes) / 16u) << 6);
	const u16 bplsize = static_cast<u16>(s.bytes / 4u);
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	switch (s.phase) {
	case 0:
		custom_base[custom_bltamod_offset] = 4;
		custom_base[custom_bltbmod_offset] = 4;
		custom_base[custom_bltdmod_offset] = 4;
		custom_base[custom_bltcdat_offset] = 0x00ff;
		custom_base[custom_bltafwm_offset] = 0xffff;
		custom_base[custom_bltalwm_offset] = 0xffff;
		write_custom_pointer(custom_bltapt_offset, src + 4);
		write_custom_pointer(custom_bltbpt_offset, src);
		write_custom_pointer(custom_bltdpt_offset, dst);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out | blt_shift8);
		custom_base[custom_bltcon1_offset] = 0;
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 1:
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 2:
		write_custom_pointer(custom_bltapt_offset, src + s.bytes - 6);
		write_custom_pointer(custom_bltbpt_offset, src + s.bytes - 2);
		write_custom_pointer(custom_bltdpt_offset, dst + s.bytes - 2);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out2 | blt_shift8);
		custom_base[custom_bltcon1_offset] = blt_reverse;
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 3:
		custom_base[custom_bltsize_offset] = static_cast<u16>(2 | h);
		break;
	case 4:
		custom_base[custom_bltamod_offset] = 6;
		custom_base[custom_bltbmod_offset] = 6;
		custom_base[custom_bltdmod_offset] = 0;
		custom_base[custom_bltcdat_offset] = 0x0f0f;
		write_custom_pointer(custom_bltapt_offset, dst + 2);
		write_custom_pointer(custom_bltbpt_offset, dst);
		write_custom_pointer(custom_bltdpt_offset, s.planes[0]);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out | blt_shift4);
		custom_base[custom_bltcon1_offset] = 0;
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 5:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 6:
		write_custom_pointer(custom_bltapt_offset, dst + 6);
		write_custom_pointer(custom_bltbpt_offset, dst + 4);
		write_custom_pointer(custom_bltdpt_offset, s.planes[2]);
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 7:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 8:
		write_custom_pointer(custom_bltapt_offset, dst + s.bytes - 8);
		write_custom_pointer(custom_bltbpt_offset, dst + s.bytes - 6);
		write_custom_pointer(custom_bltdpt_offset, s.planes[1] + bplsize - 2);
		custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_c2p_abd | blt_minterm_c2p_out2 | blt_shift4);
		custom_base[custom_bltcon1_offset] = blt_reverse;
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 9:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 10:
		write_custom_pointer(custom_bltapt_offset, dst + s.bytes - 4);
		write_custom_pointer(custom_bltbpt_offset, dst + s.bytes - 2);
		write_custom_pointer(custom_bltdpt_offset, s.planes[3] + bplsize - 2);
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 11:
		custom_base[custom_bltsize_offset] = static_cast<u16>(1 | h);
		break;
	case 12: // parcheo de BPLxPT: lo hace el llamador
	default:
		break;
	}
	s.phase++;
	return true;
}

bool MinimalBackend::c2p_4bpp_step(C2p4State& s) {
	if (!c2p_4bpp_program(s)) {
		return false;
	}
	return wait_blitter();
}

bool MinimalBackend::blitter_busy() const {
	return (custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u;
}

void MinimalBackend::set_warpmode(bool enabled) {
	warpmode(enabled ? 1 : 0);
}

} // namespace eng::amiga
