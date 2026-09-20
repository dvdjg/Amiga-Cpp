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
constexpr unsigned short custom_intreqr_offset = 0x01e / 2; // INTREQR (lectura)
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
constexpr unsigned short blt_minterm_copy_a = 0x00f0;   // D = A (canal A, con barrel shifter)
constexpr unsigned short blt_minterm_a_or_b = 0x00fc;   // D = A | B (con B = D, OR aditivo)
constexpr unsigned short blt_desc = 0x0002;             // BLTCON1 BLITREVERSE (modo descendente)

void write_custom_pointer(unsigned short word_offset, const void* pointer) {
	// Una sola escritura de 32 bits, como el original (`custom_regdef.h` declara
	// BLTxPTH/L como `void *`, y `custom_->bltcpt = ptr` es un long). En 68000
	// (big-endian) el long deja el high word en `word_offset` y el low en +1, igual
	// que dos escrituras de 16; ademas cada acceso a registro custom cuesta ~57
	// ciclos con `cpu_cycle_exact`, asi que fusionarlos ahorra ~57 por puntero.
	*reinterpret_cast<volatile eng::u32*>(&custom_base[word_offset]) =
		reinterpret_cast<eng::u32>(pointer);
}

// Servicio opcional que se ejecuta mientras se espera al Blitter (drenado de fondo).
void (*g_blitter_service)(void*, unsigned short) = nullptr;
void* g_blitter_service_user = nullptr;

// Motor de fondo por IRQ de CIA-A (nivel 2, peticion PORTS): timer A programable.
extern "C" void cia_irq();
void (*g_cia_task)(void*, unsigned short) = nullptr;
void* g_cia_task_user = nullptr;
unsigned long g_cia_old_vector = 0;
bool g_cia_installed = false;

// CIA-A: registros a 0xBFE001 + reg*0x100 (ver <hardware/cia.h> y cia_chips.md).
volatile unsigned char* ciaa_reg(unsigned short index) {
	return reinterpret_cast<volatile unsigned char*>(0xbfe001)
	     + static_cast<unsigned long>(index) * 0x100u;
}

// Handler UNICO del autovector de nivel 3 (VERTB/BLIT/COPER comparten vector). El
// engine lo usa para el tick del juego (VBlank) y para el servicio de blit.
extern "C" void level3_irq();
void (*g_vbl_task)(void*, unsigned short) = nullptr;
void* g_vbl_task_user = nullptr;
void (*g_blit_task)(void*, unsigned short) = nullptr;
void* g_blit_task_user = nullptr;
bool g_level3_installed = false;
unsigned long g_level3_old_vector = 0;

bool wait_blitter() {
	// El bit BBUSY de DMACONR baja cuando el Blitter queda libre. Camino rapido sin
	// servicio de fondo: bucle apretado, identico al `_WaitBlitter` del origen
	// (`while (dmaconr & 0x4000);`). Comprobar el servicio en cada vuelta cuesta
	// ciclos reales en efectos con muchas lineas de blit (p. ej. flatshade-convex).
	if (g_blitter_service == nullptr) {
		while ((custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u) {
		}
		return true;
	}
	// Camino con servicio: drena el fondo durante la espera, con limite anti-bloqueo.
	// Mientras gira, si hay un servicio de fondo registrado, lo ejecuta.
	eng::u32 guard = 0x00ffffffu;
	while ((custom_base[custom_dmaconr_offset] & dmaconr_blitter_busy) != 0u) {
		g_blitter_service(g_blitter_service_user,
				  static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8));
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
constexpr unsigned short blt_fill_xor = 0x0010;           // BLTCON1 FILL_XOR (area fill exclusivo)
constexpr unsigned short blt_reverse = 0x0002;
constexpr unsigned short blt_line_eor = 0x0b4a;           // BC0F_LINE_EOR (minterm 0x4a | SRCA|SRCC|DEST)
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

/// Offset de fila `y * row_bytes` con multiplicacion 16x16->32 nativa (`muls.w`),
/// evitando el `__mulsi3` que genera `(u32)y * row_bytes`. Caliente en las lineas
/// (se calcula una vez por arista y plano). Misma forma que `mul_wide` del origen.
inline eng::u32 row_offset(eng::s16 y, eng::u16 row_bytes) {
	eng::s32 r;
	const eng::s32 a = y;
	const eng::s16 b = static_cast<eng::s16>(row_bytes);
	asm("muls %2,%0" : "=d"(r) : "0"(a), "dm"(b));
	return static_cast<eng::u32>(r);
}

/// Borra una region de palabras (D=0) en un plano planar.
void blit_clear_region(eng::u8* plane, eng::u16 row_bytes, eng::u16 wx0, eng::s16 y,
		       eng::u16 words, eng::u16 h) {
	eng::u8* d = plane + row_offset(y, row_bytes) + (wx0 >> 3);
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
	eng::u8* data = plane + row_offset(y1, row_bytes) + ((x1 >> 3) & ~1);
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
	// Relleno DESCENDENTE desde la ULTIMA palabra de la region (port fiel de
	// `BlitterFillArea` de libblit: `BLITREVERSE | FILL_OR`). El relleno de area
	// propaga el "carry" segun la direccion; hacerlo descendente desde el final es
	// lo que rellena correctamente (la version ascendente filtraba/rayaba).
	eng::u8* last = plane + row_offset(static_cast<eng::s16>(y + h - 1), row_bytes) +
			(wx0 >> 3) + static_cast<eng::u32>(words - 1u) * 2u;
	const eng::u16 mod = static_cast<eng::u16>(row_bytes - words * 2u);
	wait_blitter();
	custom_base[custom_bltcon0_offset] = static_cast<eng::u16>(blt_use_a | blt_use_d | 0x00f0); // A_TO_D
	custom_base[custom_bltcon1_offset] = static_cast<eng::u16>(blt_reverse | blt_fill_or);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = mod;
	custom_base[custom_bltdmod_offset] = mod;
	write_custom_pointer(custom_bltapt_offset, last);
	write_custom_pointer(custom_bltdpt_offset, last);
	custom_base[custom_bltsize_offset] = static_cast<eng::u16>((h << 6) | words);
}

/// Cookie-cut de la mascara 1 bit a un plano de color: `set` -> D = A | D (pone a
/// 1 donde la mascara), `!set` -> D = ~A & D (borra donde la mascara).
///
/// `dst` es el inicio del PLANO (fila 0); `dst_row_stride` es el avance de una
/// fila DENTRO del plano. Con planos CONTIGUOS (un plano tras otro) vale
/// `row_bytes`; con planos INTERLEAVED (una fila de cada plano seguida) vale
/// `planes*row_bytes`, porque la fila siguiente del mismo plano está tras todos
/// los planos de la fila. `mask` es un plano 1 bit con stride `mask_row_bytes`.
void blit_mask_to_plane(eng::u8* dst, eng::u32 dst_row_stride, const eng::u8* mask,
			eng::u16 mask_row_bytes, eng::u16 wx0, eng::s16 y, eng::u16 words,
			eng::u16 h, bool set) {
	const eng::u8* m = mask + row_offset(y, mask_row_bytes) + (wx0 >> 3);
	// El avance de fila del plano cabe en 16 bits (contiguo = row_bytes; interleaved
	// = planes*row_bytes <= 240 en 6 planos): `row_offset` usa muls.w, no __mulsi3.
	eng::u8* d = dst + row_offset(y, static_cast<eng::u16>(dst_row_stride)) + (wx0 >> 3);
	const eng::u16 amod = static_cast<eng::u16>(mask_row_bytes - words * 2u);
	const eng::u16 dmod = static_cast<eng::u16>(dst_row_stride - words * 2u);
	wait_blitter();
	custom_base[custom_bltcon0_offset] = static_cast<eng::u16>(
		blt_use_a | blt_use_c | blt_use_d | (set ? blt_minterm_a_or_c : blt_minterm_not_a_and_c));
	custom_base[custom_bltcon1_offset] = 0;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = amod;
	custom_base[custom_bltcmod_offset] = dmod;
	custom_base[custom_bltdmod_offset] = dmod;
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

void MinimalBackend::wait_vblank_run(void (*thunk)(void*, u16), void* user) {
	debug_start_idle();
	// Una sola lectura de VPOSR por iteracion: la condicion y el `vpos` que recibe
	// la tarea comparten el mismo valor (no anade accesos al bus en el bucle caliente).
	u32 vposr = *vpos_long;
	while ((vposr & 0x1ff00u) == (311u << 8)) {
		if (thunk != nullptr) thunk(user, static_cast<u16>((vposr & 0x1ff00u) >> 8));
		vposr = *vpos_long;
	}
	while ((vposr & 0x1ff00u) != (311u << 8)) {
		if (thunk != nullptr) thunk(user, static_cast<u16>((vposr & 0x1ff00u) >> 8));
		vposr = *vpos_long;
	}
	debug_stop_idle();
}

void MinimalBackend::install_blitter_service(ServiceSlot& slot) {
	g_blitter_service = slot.thunk;
	g_blitter_service_user = &slot;
}

u16 MinimalBackend::current_raster_line() const {
	return static_cast<u16>((*vpos_long & 0x1ff00u) >> 8);
}

u32 MinimalBackend::cia_tod_ticks() const {
	// Orden de latch de la CIA: TODHI congela TODMID/TODLO (ver cia_chips.md).
	const u32 hi = *ciaa_reg(0x0au);   // TODHI
	const u32 mid = *ciaa_reg(0x09u);  // TODMID
	const u32 lo = *ciaa_reg(0x08u);   // TODLO
	return (hi << 16) | (mid << 8) | lo;
}

// Despacha el nivel 3: lee INTREQR y atiende VERTB (tick del juego) y BLIT (servicio de
// blit). Cada fuente limpia su propio bit antes de llamar a su tarea.
extern "C" void level3_dispatch() {
	const unsigned short req = custom_base[custom_intreqr_offset];
	const unsigned short vpos = static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8);
	if ((req & 0x0020u) != 0u) {                    // VERTB
		custom_base[custom_intreq_offset] = 0x0020u;
		if (g_vbl_task != nullptr) {
			g_vbl_task(g_vbl_task_user, vpos);
		}
	}
	if ((req & 0x0040u) != 0u) {                    // BLIT
		custom_base[custom_intreq_offset] = 0x0040u;
		if (g_blit_task != nullptr) {
			g_blit_task(g_blit_task_user, vpos);
		}
	}
}

// Instala/restaura el handler unico segun los servicios activos. Solo toca INTEN/VERTB/
// BLIT; no desarma otros bits de INTENA (p. ej. el audio del mixer).
void level3_sync() {
	const bool need = (g_vbl_task != nullptr) || (g_blit_task != nullptr);
	if (need && !g_level3_installed) {
		volatile eng::u32* const vector3 = reinterpret_cast<volatile eng::u32*>(0x6cu);
		g_level3_old_vector = *vector3;
		*vector3 = reinterpret_cast<eng::u32>(&level3_irq);
		g_level3_installed = true;
	}
	if (need) {
		unsigned short bits = 0x4000u;              // INTEN (master)
		if (g_vbl_task != nullptr) bits |= 0x0020u; // VERTB
		if (g_blit_task != nullptr) bits |= 0x0040u;// BLIT
		custom_base[custom_intena_offset] = static_cast<unsigned short>(0x8000u | bits);
	} else {
		custom_base[custom_intena_offset] = 0x0060u; // desarmar VERTB|BLIT (INTEN se deja)
		if (g_level3_installed) {
			*reinterpret_cast<volatile eng::u32*>(0x6cu) = g_level3_old_vector;
			g_level3_installed = false;
		}
	}
}

bool MinimalBackend::install_vblank_service(ServiceSlot& slot) {
	if (g_vbl_task != nullptr) {
		return false;
	}
	g_vbl_task = slot.thunk;
	g_vbl_task_user = &slot;
	level3_sync();
	return true;
}

void MinimalBackend::clear_vblank_service() {
	if (g_vbl_task == nullptr) {
		return;
	}
	custom_base[custom_intreq_offset] = 0x0020u;
	g_vbl_task = nullptr;
	g_vbl_task_user = nullptr;
	level3_sync();
}

bool MinimalBackend::install_blit_service(ServiceSlot& slot) {
	if (g_blit_task != nullptr) {
		return false;
	}
	g_blit_task = slot.thunk;
	g_blit_task_user = &slot;
	level3_sync();
	return true;
}

void MinimalBackend::clear_blit_service() {
	if (g_blit_task == nullptr) {
		return;
	}
	custom_base[custom_intreq_offset] = 0x0040u;
	g_blit_task = nullptr;
	g_blit_task_user = nullptr;
	level3_sync();
}

void MinimalBackend::set_blitter_priority(bool enabled) {
	// DMACON bit 10 (BLTPRI) = "blitter nasty": el Blitter no deja slots libres a la CPU.
	// SETCLR (0x8000) activa; sin SETCLR, el bit se limpia. No toca MASTER/BLITTER.
	custom_base[custom_dmacon_offset] = enabled ? static_cast<unsigned short>(0x8400u)
						    : static_cast<unsigned short>(0x0400u);
}

// Despachador de la IRQ de la CIA-A (nivel 2): lo llama `support/cia_irq.s`.
extern "C" void cia_dispatch() {
	(void)*ciaa_reg(0x0du);                        // leer ICR reconoce la IRQ de la CIA
	custom_base[custom_intreq_offset] = 0x0008u;   // limpiar PORTS (por si acaso)
	if (g_cia_task != nullptr) {
		g_cia_task(g_cia_task_user, static_cast<unsigned short>((*vpos_long & 0x1ff00u) >> 8));
	}
}

bool MinimalBackend::install_timer_service(u16 latch, ServiceSlot& slot) {
	if (g_cia_task != nullptr) {
		return false;
	}
	g_cia_task = slot.thunk;
	g_cia_task_user = &slot;

	// Autovector de nivel 2 (VBR=0 en 68000 -> 0x68).
	volatile eng::u32* const vector2 = reinterpret_cast<volatile eng::u32*>(0x68u);
	g_cia_old_vector = *vector2;
	*vector2 = reinterpret_cast<eng::u32>(&cia_irq);
	g_cia_installed = true;

	// Timer A **continuo** (CRA RUNMODE=0), reloj E (INMODE=0). El orden importa:
	// parar, cargar el latch, LOAD (stroby se autolimpia) y START.
	volatile unsigned char* const lo = ciaa_reg(4u);   // TALO
	volatile unsigned char* const hi = ciaa_reg(5u);   // TAHI
	volatile unsigned char* const cra = ciaa_reg(0x0eu);
	*cra = 0x00u;
	*lo = static_cast<unsigned char>(latch & 0xffu);
	*hi = static_cast<unsigned char>((latch >> 8) & 0xffu);
	*cra = 0x10u;                                      // LOAD
	*cra = 0x11u;                                      // LOAD|START (continuo)

	// Enmascara la IRQ del timer A y arma INTEN|PORTS.
	*ciaa_reg(0x0du) = 0x81u;                          // ICR: SETCLR | TA
	custom_base[custom_intena_offset] = 0xc008u;       // SETCLR | INTEN | PORTS
	return true;
}

void MinimalBackend::background_timer_stop() {
	if (g_cia_task == nullptr) {
		return;
	}
	*ciaa_reg(0x0eu) = 0x00u;                          // CRA: parar
	*ciaa_reg(0x0du) = 0x01u;                          // ICR: CLR | TA
	custom_base[custom_intena_offset] = 0x0008u;       // desarmar PORTS
	if (g_cia_installed) {
		*reinterpret_cast<volatile eng::u32*>(0x68u) = g_cia_old_vector;
		g_cia_installed = false;
	}
	(void)*ciaa_reg(0x0du);
	g_cia_task = nullptr;
	g_cia_task_user = nullptr;
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
	bool eor_open = false; // racha de líneas EOR con los registros comunes ya fijados
	for (u8 job_index = 0; job_index < plan.blit_job_count(); ++job_index) {
		const graphics::BlitJob& job = plan.blit_job(job_index);
		const bool masked =
			job.kind == graphics::BlitJobKind::MaskedBobCookieCut ||
			job.kind == graphics::BlitJobKind::MaskedBlobNoSave;
		const bool copy =
			job.kind == graphics::BlitJobKind::CopyRect ||
			job.kind == graphics::BlitJobKind::RestoreRect ||
			job.kind == graphics::BlitJobKind::TileBlockCopy;
		const bool clear = job.kind == graphics::BlitJobKind::ClearRect;
		const bool or_blob = job.kind == graphics::BlitJobKind::OrBlob ||
				     job.kind == graphics::BlitJobKind::PatternFill;
		const bool logic = job.kind == graphics::BlitJobKind::LogicBlit;
		const bool line = job.kind == graphics::BlitJobKind::Line;
		const bool line_eor = job.kind == graphics::BlitJobKind::LineEor;

		if (!masked && !copy && !clear && !or_blob && !logic && !line && !line_eor) {
			return false;
		}

		if (line || line_eor) {
			// Línea por Blitter (LINE) o EOR/ONEDOT sobre el plano del `destination`.
			eng::PlaneBytes pb {reinterpret_cast<eng::u8*>(job.destination.words), 0u};
			eng::u8* d_base = job.line_base.words != nullptr
						  ? reinterpret_cast<eng::u8*>(job.line_base.words)
						  : nullptr;
			if (line) {
				eor_open = false;
				if (!blitter_line(pb, job.line_row_bytes, job.line_x0, job.line_y0,
						  job.line_x1, job.line_y1)) {
					return false;
				}
			} else {
				// Racha EOR: fija los registros comunes UNA vez (no por arista×plano).
				if (!eor_open) {
					blitter_lines_eor_begin(job.line_row_bytes);
					eor_open = true;
				}
				LineEorParams p;
				if (blitter_line_eor_prepare(p, job.line_row_bytes, job.line_x0,
							     job.line_y0, job.line_x1, job.line_y1)) {
					blitter_line_eor_draw(p, reinterpret_cast<eng::u8*>(job.destination.words),
							      d_base);
				}
			}
			continue;
		}
		eor_open = false; // cualquier otro job cierra la racha EOR

		custom_base[custom_dmacon_offset] = dma_setclr | dma_master | dma_blitter;

		const u32 source_plane_stride_words = job.source_plane_stride_bytes / sizeof(u16);
		const u32 destination_plane_stride_words = job.destination_plane_stride_bytes / sizeof(u16);
		for (u8 plane = 0; plane < job.bitplane_count; ++plane) {
			if (!wait_blitter()) {
				return false;
			}

			const u16* source_plane = job.source.words + static_cast<u32>(plane) * source_plane_stride_words;
			u16* destination_plane = job.destination.words + static_cast<u32>(plane) * destination_plane_stride_words;

			if (clear) {
				// Solo D con el minterm del job (por defecto `$00` = D=0): un blit
				// borra la caja del objeto, y con bitmaps intercalados cubre los
				// planos en ese mismo blit.
				custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_d | job.minterm);
				custom_base[custom_bltcon1_offset] = 0;
				custom_base[custom_bltafwm_offset] = 0xffff;
				custom_base[custom_bltalwm_offset] = 0xffff;
				custom_base[custom_bltamod_offset] = 0;
				custom_base[custom_bltbmod_offset] = 0;
				custom_base[custom_bltcmod_offset] = 0;
				custom_base[custom_bltdmod_offset] = static_cast<u16>(job.destination_modulo_bytes);
				write_custom_pointer(custom_bltdpt_offset, destination_plane);
				custom_base[custom_bltsize_offset] = static_cast<u16>(
					(static_cast<u16>(job.height) << 6) | job.words_per_row
				);
				++m_blitter_starts;
				continue;
			}
			if (masked) {
				custom_base[custom_bltcon0_offset] = static_cast<u16>(
					(static_cast<u16>(job.source_shift) << 12u) |
					blt_use_a | blt_use_b | blt_use_c | blt_use_d | job.minterm
				);
				custom_base[custom_bltcon1_offset] = static_cast<u16>(
					static_cast<u16>(job.source_shift) << 12u
				);
			} else if (or_blob || logic) {
				// BOB OR (bobs3d) o blit lógico: A = objeto (con barrel shift ASH),
				// B = D = destino, minterm del job (`$FC` D=A|D, `$80` A&D, `$60` A^D).
				// El canal C no interviene.
				//
				// OJO: BLTCON1 bits 15-12 son **BSH** (shift del canal B), NO un duplicado
				// de ASH. B aqui es el DESTINO (B=D), asi que poner BSH!=0 desplaza la
				// lectura del fondo y emborrona el BOB (cola horizontal). El original
				// (`bobs3d.c`) deja `bltcon1=0`; solo desplaza A via BLTCON0. Ver AHRM 3. a
				// (BLTCON1) y `amiga-bootcamp/08_graphics/blitter_programming.md` ("Shift
				// and Alignment").
				custom_base[custom_bltcon0_offset] = static_cast<u16>(
					(static_cast<u16>(job.source_shift) << 12u) |
					blt_use_a | blt_use_b | blt_use_d | job.minterm
				);
				custom_base[custom_bltcon1_offset] = 0u;
			} else if (job.source_shift != 0u) {
				// Copia con desplazamiento fino. El barrel shifter del Blitter solo
				// actua sobre los canales A y B (AHRM 6, "Shifting"), asi que la
				// fuente va por A y el minterm es D=A ($F0). El llamador apunta A a la
				// word `q = src_x + S` y fija `source_shift` = S (0..15): en modo
				// ascendente (DESC=0) el Blitter desplaza a la derecha y
				// `destino[d] = patron[q + d - S]`, de modo que `destino[0]` lee el
				// pixel `src_x` = q - S.
				custom_base[custom_bltcon0_offset] = static_cast<u16>(
					(static_cast<u16>(job.source_shift) << 12u) |
					blt_use_a | blt_use_d | blt_minterm_copy_a
				);
				custom_base[custom_bltcon1_offset] = static_cast<u16>(
					(static_cast<u16>(job.source_shift) << 12u) |
					(job.descending ? blt_desc : 0x0000)
				);
			} else {
				custom_base[custom_bltcon0_offset] = static_cast<u16>(
					blt_use_c | blt_use_d | blt_minterm_copy_c
				);
				custom_base[custom_bltcon1_offset] = static_cast<u16>(
					job.descending ? blt_desc : 0x0000
				);
			}
			const bool shifted_copy = !masked && !or_blob && !logic && job.source_shift != 0u;
			const bool source_by_a = masked || or_blob || logic || shifted_copy;
			// En copias/OR con shift, la ultima word de cada fila se enmascara para que
			// los bits desplazados hacia fuera (que el Blitter reinyecta al principio
			// de la fila siguiente) sean cero: deja una guarda de `shift` px al
			// principio del bitmap, nunca datos erroneos de la fila anterior.
			custom_base[custom_bltafwm_offset] = 0xffff;
			custom_base[custom_bltalwm_offset] = (shifted_copy || or_blob || logic)
				? static_cast<u16>(0xffffu << job.source_shift)
				: 0xffff;
			custom_base[custom_bltamod_offset] = static_cast<u16>(source_by_a ? job.source_modulo_bytes : 0);
			custom_base[custom_bltbmod_offset] = static_cast<u16>(
				masked ? job.source_modulo_bytes : ((or_blob || logic) ? job.destination_modulo_bytes : 0));
			custom_base[custom_bltcmod_offset] = static_cast<u16>(masked ? job.destination_modulo_bytes : job.source_modulo_bytes);
			custom_base[custom_bltdmod_offset] = static_cast<u16>(job.destination_modulo_bytes);

			if (masked) {
				write_custom_pointer(custom_bltapt_offset, job.mask.words);
				write_custom_pointer(custom_bltbpt_offset, source_plane);
				write_custom_pointer(custom_bltcpt_offset, destination_plane);
			} else if (or_blob || logic) {
				// B = D = destino (el mismo puntero): aplica el minterm del job.
				write_custom_pointer(custom_bltapt_offset, source_plane);
				write_custom_pointer(custom_bltbpt_offset, destination_plane);
			} else if (shifted_copy) {
				write_custom_pointer(custom_bltapt_offset, source_plane);
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
					    eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes,
					    eng::MaskBuffer mask) {
	if (tris == nullptr || dst.data() == nullptr || mask.data() == nullptr || planes == 0u) {
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
		blit_clear_region(mask.data(), row_bytes, wx0, ymin, words, h);
		blit_line(mask.data(), row_bytes, t.x0, t.y0, t.x1, t.y1);
		blit_line(mask.data(), row_bytes, t.x1, t.y1, t.x2, t.y2);
		blit_line(mask.data(), row_bytes, t.x2, t.y2, t.x0, t.y0);
		blit_fill_region(mask.data(), row_bytes, wx0, ymin, words, h);
		for (u8 p = 0; p < planes; ++p) {
			blit_mask_to_plane(dst.data() + static_cast<u32>(p) * plane_bytes, row_bytes, mask.data(),
					   row_bytes, wx0, ymin, words, h, ((t.color >> p) & 1u) != 0u);
		}
	}
	return wait_blitter();
}

bool MinimalBackend::blitter_fill_polygon(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes,
					  const s16* xs, const s16* ys, u8 n, u8 color, eng::MaskBuffer mask) {
	if (dst.data() == nullptr) {
		return false;
	}
	// Planos CONTIGUOS: la base del plano p está a `plane_bytes` del anterior y
	// cada fila avanza `row_bytes`. Se delega en la ruta con strides explícitos.
	return blitter_fill_polygon_strided(dst.data(), planes, plane_bytes, row_bytes, row_bytes,
					    320, 256, xs, ys, n, color, mask);
}

bool MinimalBackend::blitter_fill_polygon_strided(eng::u8* plane_base, u8 planes, u32 plane_stride,
						  u32 row_stride, u16 row_bytes, u16 bitmap_w, u16 bitmap_h,
						  const s16* xs, const s16* ys, u8 n, u8 color, eng::MaskBuffer mask) {
	if (plane_base == nullptr || mask.data() == nullptr || xs == nullptr || ys == nullptr ||
	    n < 3u || planes == 0u || bitmap_w == 0u || bitmap_h == 0u) {
		return false;
	}
	s16 xmin = xs[0], xmax = xs[0], ymin = ys[0], ymax = ys[0];
	for (u8 i = 1u; i < n; ++i) {
		if (xs[i] < xmin) xmin = xs[i];
		if (xs[i] > xmax) xmax = xs[i];
		if (ys[i] < ymin) ymin = ys[i];
		if (ys[i] > ymax) ymax = ys[i];
	}
	const s16 max_x = static_cast<s16>(bitmap_w - 1u);
	const s16 max_y = static_cast<s16>(bitmap_h - 1u);
	if (xmax < 0 || ymax < 0 || xmin > max_x || ymin > max_y) {
		return true;
	}
	if (xmin < 0) xmin = 0;
	if (ymin < 0) ymin = 0;
	if (xmax > max_x) xmax = max_x;
	if (ymax > max_y) ymax = max_y;
	const u16 wx0 = static_cast<u16>(xmin) & 0xfff0u;
	const u16 wx1 = static_cast<u16>(xmax) | 0x000fu;
	const u16 words = static_cast<u16>((static_cast<u16>(wx1 - wx0) + 16u) >> 4);
	const u16 h = static_cast<u16>(ymax - ymin + 1);

	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	blit_clear_region(mask.data(), row_bytes, wx0, ymin, words, h);
	for (u8 i = 0u; i < n; ++i) {
		const u8 j = static_cast<u8>((static_cast<u8>(i) + 1u) % n);
		blit_line(mask.data(), row_bytes, xs[i], ys[i], xs[j], ys[j]);
	}
	blit_fill_region(mask.data(), row_bytes, wx0, ymin, words, h);
	for (u8 p = 0u; p < planes; ++p) {
		eng::u8* d = plane_base + static_cast<u32>(p) * plane_stride;
		blit_mask_to_plane(d, row_stride, mask.data(), row_bytes, wx0, ymin, words, h,
				   ((color >> p) & 1u) != 0u);
	}
	return wait_blitter();
}

bool MinimalBackend::blit_fill_from_mask(eng::MaskBytes mask, eng::PlaneBytes dst, u8 planes, u16 row_bytes,
					 u32 plane_bytes, s16 x, s16 y, u16 w, u16 h, u8 color) {
	if (mask.data() == nullptr || dst.data() == nullptr || planes == 0u || w == 0u || h == 0u) {
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
		blit_mask_to_plane(dst.data() + static_cast<u32>(p) * plane_bytes, row_bytes, mask.data(),
				   row_bytes, wx0, y0, words, hh, ((color >> p) & 1u) != 0u);
	}
	return wait_blitter();
}

bool MinimalBackend::blitter_line(eng::PlaneBytes plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1) {
	if (plane.data() == nullptr) {
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

	u8* data = plane.data() + row_offset(y0, row_bytes) + (static_cast<u32>(x0) >> 3);
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

bool MinimalBackend::blitter_collide(eng::PlaneBytes a, eng::PlaneBytes b, eng::PlaneBytes scratch,
				     u8 planes, u16 row_bytes, u32 plane_bytes, u16 words, u16 rows) {
	if (a.data() == nullptr || b.data() == nullptr || scratch.data() == nullptr ||
	    planes == 0u || words == 0u || rows == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	// D = A & B (minterm $C0) por plano, a un scratch; luego escaneo CPU del scratch.
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_b | blt_use_d | 0x00c0);
	custom_base[custom_bltcon1_offset] = 0;
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltamod_offset] = static_cast<u16>(row_bytes - words * 2u);
	custom_base[custom_bltbmod_offset] = static_cast<u16>(row_bytes - words * 2u);
	custom_base[custom_bltdmod_offset] = static_cast<u16>(row_bytes - words * 2u);
	for (u8 p = 0; p < planes; ++p) {
		if (!wait_blitter()) return false;
		write_custom_pointer(custom_bltapt_offset, a.data() + static_cast<u32>(p) * plane_bytes);
		write_custom_pointer(custom_bltbpt_offset, b.data() + static_cast<u32>(p) * plane_bytes);
		write_custom_pointer(custom_bltdpt_offset, scratch.data() + static_cast<u32>(p) * plane_bytes);
		custom_base[custom_bltsize_offset] =
			static_cast<u16>((static_cast<u16>(rows) << 6u) | words);
		++m_blitter_starts;
	}
	if (!wait_blitter()) return false;
	for (u8 p = 0; p < planes; ++p) {
		const eng::u8* s = scratch.data() + static_cast<u32>(p) * plane_bytes;
		for (u16 r = 0; r < rows; ++r) {
			const u16* w = reinterpret_cast<const u16*>(s + static_cast<u32>(r) * row_bytes);
			for (u16 i = 0; i < words; ++i) {
				if (w[i] != 0u) return true;
			}
		}
	}
	return false;
}

bool MinimalBackend::fill_polygons_by_plane(const graphics::PlanePolygon* faces, u32 n_faces,
					    eng::PlaneBytes dest, u16 row_bytes, u32 plane_bytes,
					    u8 planes, u16 width, u16 height) {
	if (faces == nullptr || n_faces == 0u || dest.data() == nullptr || planes == 0u ||
	    row_bytes == 0u || plane_bytes == 0u) {
		return false;
	}
	for (u8 p = 0; p < planes; ++p) {
		eng::PlaneBytes plane =
			dest.subspan(static_cast<u32>(p) * plane_bytes, plane_bytes);
		// 1) limpia el plano.
		blitter_clear(plane, 1u, row_bytes, plane_bytes, width, height);
		// 2) contorno XOR (ONEDOT) de las caras cuyo color tiene el bit `p` a 1; las
		//    aristas compartidas por dos caras del mismo bit se dibujan dos veces y el
		//    fill even-odd las cancela. Los registros comunes EOR se fijan UNA vez por
		//    plano (`blitter_lines_eor_begin`), no por arista.
		blitter_lines_eor_begin(row_bytes);
		for (u32 f = 0; f < n_faces; ++f) {
			const graphics::PlanePolygon& face = faces[f];
			if ((face.color & (1u << p)) == 0u || face.count < 3u) {
				continue;
			}
			for (u8 i = 0; i < face.count; ++i) {
				const u8 j = static_cast<u8>((i + 1u) % face.count);
				LineEorParams eor;
				if (blitter_line_eor_prepare(eor, row_bytes, face.xs[i], face.ys[i],
							     face.xs[j], face.ys[j])) {
					blitter_line_eor_draw(eor, plane.data(), plane.data());
				}
			}
		}
		// 3) area fill (FILL_XOR) del plano in situ (un fill por plano). Se acota al
		//    plano (`blitter_area_fill_rect`): `blitter_area_fill` barre 1024 filas
		//    (pensado para el bitmap multicapa contiguo de 116) y desbordaria el plano.
		blitter_area_fill_rect(plane, row_bytes, 0, 0, static_cast<u16>(width / 16u), height,
				       true);
	}
	return true;
}

bool MinimalBackend::blitter_line_eor(eng::PlaneBytes plane, u16 row_bytes, s16 x0, s16 y0, s16 x1, s16 y1,
				      eng::u8* d_base) {
	// Variante autónoma: begin + prepare/draw + wait final.
	blitter_lines_eor_begin(row_bytes);
	LineEorParams p;
	if (blitter_line_eor_prepare(p, row_bytes, x0, y0, x1, y1)) {
		blitter_line_eor_draw(p, plane.data(), d_base);
	}
	return wait_blitter();
}

void MinimalBackend::blitter_lines_eor_begin(u16 row_bytes) {
	// Setup común para una secuencia de líneas EOR (ONEDOT), equivalente al preludio
	// de `DrawObject` en flatshade-convex (`bltafwm/bltalwm=-1, bltadat=0x8000,
	// bltbdat=0xffff, bltcmod/bltdmod=WIDTH/8`). Se fija UNA vez por grupo de líneas:
	// cada escritura a registro custom cuesta ~57 ciclos con `cpu_cycle_exact`, así
	// que reescribirlos por arista×plano es desperdicio (el original no lo hace).
	// NO espera al Blitter: el primer `blitter_line_eor_draw` (que sí espera)
	// sincroniza con cualquier blit previo (el clear).
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltadat_offset] = 0x8000;
	custom_base[custom_bltbdat_offset] = 0xffff;
	custom_base[custom_bltcmod_offset] = row_bytes;
	custom_base[custom_bltdmod_offset] = row_bytes;
}

bool MinimalBackend::blitter_line_eor_prepare(LineEorParams& out, u16 row_bytes, s16 x0, s16 y0,
					      s16 x1, s16 y1) {
	// El original (`DrawObject` de flatshade-convex) DESCARTA las aristas
	// horizontales: no aportan contorno util y, dibujadas, meterian píxeles
	// extra en los vertices que descuadran el area fill (cruces impares).
	if (y0 == y1) {
		return false;
	}
	if (y0 > y1) {
		s16 t = x0; x0 = x1; x1 = t;
		t = y0; y0 = y1; y1 = t;
	}
	s16 dmax = static_cast<s16>(x1 - x0);
	s16 dmin = static_cast<s16>(y1 - y0);
	u16 bltcon1 = static_cast<u16>(blt_linemode | blt_onedot);
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
	out.row_offset = row_offset(y0, row_bytes) + ((static_cast<u32>(x0) >> 3) & ~1u);
	out.bltcon0 = static_cast<u16>(ror16(static_cast<u16>(x0 & 15), 4) | blt_line_eor);
	out.bltcon1 = static_cast<u16>(bltcon1 | ror16(static_cast<u16>(x0 & 15), 4));
	dmin = static_cast<s16>(dmin << 1);
	out.derr = static_cast<s16>(dmin - dmax);
	out.bltamod = static_cast<u16>(out.derr - dmax);
	out.bltbmod = static_cast<u16>(dmin);
	out.bltsize = static_cast<u16>((static_cast<u16>(dmax) << 6) + 66u);
	return true;
}

void MinimalBackend::blitter_line_eor_draw(const LineEorParams& p, eng::u8* plane_ptr, eng::u8* d_base) {
	u8* data = plane_ptr + p.row_offset;
	wait_blitter();
	custom_base[custom_bltcon0_offset] = p.bltcon0;
	custom_base[custom_bltcon1_offset] = p.bltcon1;
	custom_base[custom_bltamod_offset] = p.bltamod;
	custom_base[custom_bltbmod_offset] = p.bltbmod;
	write_custom_pointer(custom_bltapt_offset,
			     reinterpret_cast<void*>(static_cast<u32>(static_cast<s32>(p.derr))));
	write_custom_pointer(custom_bltcpt_offset, data);
	write_custom_pointer(custom_bltdpt_offset, d_base != nullptr ? d_base : data);
	custom_base[custom_bltsize_offset] = p.bltsize;
	// Sin esperar aqui: la siguiente operacion (o el swap de copperlist) sincroniza.
}

bool MinimalBackend::blitter_area_fill(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 width, u16 height,
				       bool wait) {
	if (dst.data() == nullptr || planes == 0u || width < 16u || height == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	// Semilla = ultima palabra del bitmap (planos contiguos), descendente. El area
	// fill recorre `height` filas conmutando el bit de relleno en cada pixel del
	// contorno, de modo que rellena el interior (port de `BlitterFillArea`; el
	// original `flatshade-convex` usaba altura 0 por un bug, insuficiente).
	u8* bltpt = dst.data() + static_cast<u32>(plane_bytes) * planes - 2u;
	const u16 bltsize = static_cast<u16>((0u << 6) | (width >> 4));
	wait_blitter();
	write_custom_pointer(custom_bltapt_offset, bltpt);
	write_custom_pointer(custom_bltdpt_offset, bltpt);
	custom_base[custom_bltamod_offset] = 0;
	custom_base[custom_bltdmod_offset] = 0;
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_d | blt_minterm_copy_a);
	custom_base[custom_bltcon1_offset] = static_cast<u16>(blt_reverse | blt_fill_xor);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltsize_offset] = bltsize;
	return wait ? wait_blitter() : true;
}

bool MinimalBackend::blitter_clear(eng::PlaneBytes dst, u8 planes, u16 row_bytes, u32 plane_bytes, u16 w, u16 h,
				   bool wait) {
	if (dst.data() == nullptr || planes == 0u || w < 16u || h == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	const u16 words = static_cast<u16>(w / 16u);
	// Planos contiguos con filas contiguas (`plane_bytes == row_bytes*h`): UN solo
	// blit barre los `planes` planos de una pasada (como `BitmapClearFast` del
	// original: altura 0 = 1024 lineas si `planes*h` desborda los 10 bits). Si no,
	// se limpia plano a plano.
	if (static_cast<u32>(row_bytes) * h == plane_bytes) {
		blit_clear_region(dst.data(), row_bytes, 0, 0, words, static_cast<u16>(planes * h));
	} else {
		for (u8 p = 0; p < planes; ++p) {
			blit_clear_region(dst.data() + static_cast<u32>(p) * plane_bytes, row_bytes, 0, 0, words, h);
		}
	}
	return wait ? wait_blitter() : true;
}

void MinimalBackend::blitter_or_bobs_begin(u16 words, u16 height, s16 source_modulo,
					   s16 dest_modulo) {
	// Misma implementacion que el camino `inline` de coste cero (blob.hpp): una sola
	// fuente de verdad para la secuencia de registros.
	m_or_bob.begin(custom_base, words, height, source_modulo, dest_modulo);
}

void MinimalBackend::blitter_or_bobs_one(const void* source, void* dest, u8 shift) {
	m_or_bob.one(source, dest, shift);
}

bool MinimalBackend::blitter_or_bobs_end() {
	return m_or_bob.end();
}

bool MinimalBackend::blitter_or_bobs(const OrBobEntry* entries, u32 count, u16 words, u16 height,
				     s16 source_modulo, s16 dest_modulo) {
	if (entries == nullptr || count == 0u || words == 0u || height == 0u) {
		return false;
	}
	blitter_or_bobs_begin(words, height, source_modulo, dest_modulo);
	for (u32 i = 0; i < count; ++i) {
		blitter_or_bobs_one(entries[i].source, entries[i].dest, entries[i].shift);
	}
	return blitter_or_bobs_end();
}

bool MinimalBackend::blitter_clear_rect(eng::PlaneBytes plane, u16 row_bytes, u16 wx0, s16 y0, u16 words, u16 rows,
					bool wait) {
	if (plane.data() == nullptr || words == 0u || rows == 0u) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	blit_clear_region(plane.data(), row_bytes, wx0, y0, words, rows);
	return wait ? wait_blitter() : true;
}

bool MinimalBackend::blitter_area_fill_rect(eng::PlaneBytes plane, u16 row_bytes, u16 wx0, s16 y0, u16 words, u16 rows,
					    bool wait) {
	if (plane.data() == nullptr || words == 0u || rows == 0u || words * 2u > row_bytes) {
		return false;
	}
	custom_base[custom_dmacon_offset] = static_cast<u16>(dma_setclr | dma_master | dma_blitter);
	const u16 mod = static_cast<u16>(row_bytes - words * 2u);
	eng::u8* seed = plane.data() + row_offset(static_cast<eng::s16>(y0 + rows - 1), row_bytes) +
			(wx0 >> 3) + (words - 1u) * 2u;
	wait_blitter();
	write_custom_pointer(custom_bltapt_offset, seed);
	write_custom_pointer(custom_bltdpt_offset, seed);
	custom_base[custom_bltamod_offset] = mod;
	custom_base[custom_bltdmod_offset] = mod;
	custom_base[custom_bltcon0_offset] = static_cast<u16>(blt_use_a | blt_use_d | blt_minterm_copy_a);
	custom_base[custom_bltcon1_offset] = static_cast<u16>(blt_reverse | blt_fill_xor);
	custom_base[custom_bltafwm_offset] = 0xffff;
	custom_base[custom_bltalwm_offset] = 0xffff;
	custom_base[custom_bltsize_offset] = static_cast<u16>((rows << 6) | words);
	return wait ? wait_blitter() : true;
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

bool MinimalBackend::wait_blitter() {
	return ::wait_blitter();
}

void MinimalBackend::set_warpmode(bool enabled) {
	warpmode(enabled ? 1 : 0);
}

} // namespace eng::amiga
