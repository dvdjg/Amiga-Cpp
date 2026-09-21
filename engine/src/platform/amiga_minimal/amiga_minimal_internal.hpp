#pragma once

/// \file amiga_minimal_internal.hpp
/// Helpers internos del backend Amiga (registros custom, espera de Blitter, regiones
/// planares y despacho de IRQ). Se extraen de `amiga_minimal.cpp` para que los servicios
/// (core, blitter, C2P) vivan en unidades de traduccion separadas compartiendo los mismos
/// helpers. **No forma parte de la API**: la incluyen solo los .cpp del backend.

#include <eng/platform/amiga_minimal.hpp>

#include "support/gcc8_c_support.h"
#include <proto/exec.h>
#include <exec/memory.h>

// Handlers de IRQ definidos en ASM (`support/`); el backend los instala por vector.
extern "C" {
void cia_irq();
void level3_irq();
}

namespace eng::amiga::detail {


inline volatile unsigned short* const custom_base = reinterpret_cast<volatile unsigned short*>(0xdff000);
inline volatile unsigned long* const vpos_long = reinterpret_cast<volatile unsigned long*>(0xdff004);
inline volatile unsigned long* const cop1lc = reinterpret_cast<volatile unsigned long*>(0xdff080);

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

inline void write_custom_pointer(unsigned short word_offset, const void* pointer) {
	// Una sola escritura de 32 bits, como el original (`custom_regdef.h` declara
	// BLTxPTH/L como `void *`, y `custom_->bltcpt = ptr` es un long). En 68000
	// (big-endian) el long deja el high word en `word_offset` y el low en +1, igual
	// que dos escrituras de 16; ademas cada acceso a registro custom cuesta ~57
	// ciclos con `cpu_cycle_exact`, asi que fusionarlos ahorra ~57 por puntero.
	*reinterpret_cast<volatile eng::u32*>(&custom_base[word_offset]) =
		reinterpret_cast<eng::u32>(pointer);
}

// Servicio opcional que se ejecuta mientras se espera al Blitter (drenado de fondo).
inline void (*g_blitter_service)(void*, unsigned short) = nullptr;
inline void* g_blitter_service_user = nullptr;

// Motor de fondo por IRQ de CIA-A (nivel 2, peticion PORTS): timer A programable.
inline void (*g_cia_task)(void*, unsigned short) = nullptr;
inline void* g_cia_task_user = nullptr;
inline unsigned long g_cia_old_vector = 0;
inline bool g_cia_installed = false;

// Teclado del mini-SO (CIA-A serie, SP): el despachador lo llama si la IRQ es de SP.
inline void (*g_os_kbd_isr)() = nullptr;

// CIA-A: registros a 0xBFE001 + reg*0x100 (ver <hardware/cia.h> y cia_chips.md).
inline volatile unsigned char* ciaa_reg(unsigned short index) {
	return reinterpret_cast<volatile unsigned char*>(0xbfe001)
	     + static_cast<unsigned long>(index) * 0x100u;
}

// Handler UNICO del autovector de nivel 3 (VERTB/BLIT/COPER comparten vector). El
// engine lo usa para el tick del juego (VBlank) y para el servicio de blit.
inline void (*g_vbl_task)(void*, unsigned short) = nullptr;
inline void* g_vbl_task_user = nullptr;
inline void (*g_blit_task)(void*, unsigned short) = nullptr;
inline void* g_blit_task_user = nullptr;
inline bool g_level3_installed = false;
inline unsigned long g_level3_old_vector = 0;

inline bool wait_blitter() {
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

inline unsigned short ror16(unsigned short value, unsigned short n) {
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
inline void blit_clear_region(eng::u8* plane, eng::u16 row_bytes, eng::u16 wx0, eng::s16 y,
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
inline void blit_line(eng::u8* plane, eng::u16 row_bytes, eng::s16 x1, eng::s16 y1,
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
inline void blit_fill_region(eng::u8* plane, eng::u16 row_bytes, eng::u16 wx0, eng::s16 y,
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
inline void blit_mask_to_plane(eng::u8* dst, eng::u32 dst_row_stride, const eng::u8* mask,
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


} // namespace eng::amiga::detail
