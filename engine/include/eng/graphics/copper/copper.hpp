#pragma once

/// \file copper.hpp
/// Constructor didactico de copperlists OCS.
///
/// El Copper ejecuta una lista de instrucciones en Chip RAM. Cada instruccion ocupa
/// dos words de 16 bits:
///
/// - `MOVE`: word0 = offset de registro custom, word1 = valor.
/// - `WAIT`: word0 = posicion vertical/horizontal, word1 = mascara/comparacion.
/// - fin de lista: `0xffff, 0xfffe`.
///
/// Esta clase no intenta ser todavia un scheduler completo. Es el primer ladrillo:
/// una forma segura de construir listas pequenas sin que cada demo escriba words
/// magicas a mano. Mas adelante el `CopperScheduler` compondra contribuciones de
/// drivers, efectos UAF y patches runtime.

#include <eng/core/types.hpp>
#include <eng/memory/arena.hpp>

namespace eng::copper {

/// Offsets de registros custom usados por las primeras demos.
///
/// Los valores son offsets desde la base custom `$dff000`. Coinciden con los campos
/// del Hardware Reference Manual y con `offsetof(struct Custom, campo)`.
enum class Register : u16 {
	COP1LCH = 0x080,
	COP1LCL = 0x082,
	COP2LCH = 0x084,
	COP2LCL = 0x086,
	COPJMP1 = 0x088,
	COPJMP2 = 0x08a,
	DIWSTRT = 0x08e,
	DIWSTOP = 0x090,
	DDFSTRT = 0x092,
	DDFSTOP = 0x094,
	DMACON = 0x096,
	BPL1PTH = 0x0e0,
	BPL1PTL = 0x0e2,
	BPL2PTH = 0x0e4,
	BPL2PTL = 0x0e6,
	BPL3PTH = 0x0e8,
	BPL3PTL = 0x0ea,
	BPL4PTH = 0x0ec,
	BPL4PTL = 0x0ee,
	BPL5PTH = 0x0f0,
	BPL5PTL = 0x0f2,
	BPL6PTH = 0x0f4,
	BPL6PTL = 0x0f6,
	BPLCON0 = 0x100,
	BPLCON1 = 0x102,
	BPLCON2 = 0x104,
	BPLCON4 = 0x106,
	BPL1MOD = 0x108,
	BPL2MOD = 0x10a,
	COLOR00 = 0x180,
};

/// Flags basicos de DMACON.
///
/// DMACON usa el bit 15 como selector set/clear: si esta a 1, los bits indicados
/// se activan; si esta a 0, se limpian. Aqui solo exponemos lo que necesita la demo
/// inicial del Copper.
enum DmaControl : u16 {
	DmaSetClear = 0x8000,
	DmaMaster = 0x0200,
	DmaBitplane = 0x0100,
	DmaCopper = 0x0080,
	DmaBlitter = 0x0040,
	DmaSprite = 0x0020,   // SPREN: habilita el DMA de todos los sprites
	DmaDisk = 0x0010,
};

/// Devuelve el offset de un registro COLORxx.
constexpr u16 color_register(u8 index) {
	return static_cast<u16>(Register::COLOR00) + static_cast<u16>(index) * 2u;
}

/// Devuelve el offset del word alto del puntero de un bitplane.
///
/// Los punteros BPLxPT son pares de registros `PTH/PTL`. El Copper solo puede
/// escribir words, asi que cargar un puntero requiere dos MOVEs. `plane` usa base
/// cero para encajar con arrays C++: 0 = BPL1, 5 = BPL6.
constexpr u16 bitplane_pointer_high_register(u8 plane) {
	return static_cast<u16>(Register::BPL1PTH) + static_cast<u16>(plane) * 4u;
}

/// Devuelve el offset del word bajo del puntero de un bitplane.
constexpr u16 bitplane_pointer_low_register(u8 plane) {
	return static_cast<u16>(Register::BPL1PTL) + static_cast<u16>(plane) * 4u;
}

/// Codifica la primera word de un WAIT sencillo.
///
/// Para las demos tempranas usamos `h = 1`, igual que los ejemplos clasicos:
/// `0x4001` espera aproximadamente a la linea `$40`.
constexpr u16 wait_word(u8 vpos, u8 hpos = 1) {
	return static_cast<u16>((static_cast<u16>(vpos) << 8) | (hpos & 0xfe) | 1u);
}

/// Builder lineal para copperlists.
///
/// No reserva memoria: escribe sobre un `MemoryBlock` que debe vivir en Chip RAM.
/// Esto mantiene visible la regla mas importante del Copper: Agnus solo puede leer
/// la lista desde Chip RAM.
class ListBuilder {
public:
	constexpr ListBuilder() = default;

	explicit ListBuilder(MemoryBlock block)
		: m_words(static_cast<u16*>(block.data)),
		  m_capacity_words(block.size / sizeof(u16)),
		  m_ok(block.valid() && block.kind == MemoryKind::Chip) {}

	/// Escribe un MOVE Copper: registro custom -> valor.
	void move(Register reg, u16 value) {
		move(static_cast<u16>(reg), value);
	}

	/// Escribe un MOVE Copper usando un offset raw.
	void move(u16 custom_register_offset, u16 value) {
		write_pair(custom_register_offset, value);
	}

	/// Escribe los dos MOVEs necesarios para cargar un puntero de bitplane.
	///
	/// Esta funcion no valida que la direccion apunte a Chip RAM: esa garantia debe
	/// venir de la arena usada por el driver. Aqui solo codificamos el formato que
	/// espera Agnus en BPLxPTH/BPLxPTL.
	void move_bitplane_pointer(u8 plane, const void* address) {
		const uintptr raw = reinterpret_cast<uintptr>(address);
		move(bitplane_pointer_high_register(plane), static_cast<u16>(raw >> 16));
		move(bitplane_pointer_low_register(plane), static_cast<u16>(raw & 0xffffu));
	}

	/// Espera a una linea de raster con mascara estandar (solo V).
	///
	/// Usa la mascara `0xff00`: compara los 8 bits verticales e ignora la posicion
	/// horizontal. Es la espera mas comun (cambios al principio de linea).
	void wait_line(u8 vpos) {
		write_pair(wait_word(vpos), 0xff00);
	}

	/// Espera a una linea PAL completa (0..311) manejando el overflow del contador V.
	///
	/// Port de `CopWaitSafe` de demoscene-repo (libgfx, `include/copper.h`). El
	/// contador vertical del Copper (VPOS) tiene 9 bits pero el campo VP del WAIT solo
	/// 8: al esperar una linea >= 256 el byte bajo "wraps". La tecnica clasica es
	/// insertar ANTES el par `0xffdf/0xfffe` (esperar al final del campo V=255 justo
	/// antes de que VPOS haga wrap), y luego el WAIT morir con el byte bajo de `vpos`.
	/// El par de overflow solo se emite la primera vez por lista (`m_overflow_sent`).
	void wait_line_pal(u16 vpos) {
		if (vpos <= 255u) {
			wait_line(static_cast<u8>(vpos & 0xffu));
			return;
		}
		if (!m_overflow_sent) {
			m_overflow_sent = true;
			// Espera al final de la linea 255: fuerza el wrap del bit 8 de VPOS.
			write_pair(0xffdf, 0xfffe);
		}
		write_pair(wait_word(static_cast<u8>(vpos & 0xffu)), 0xfffe);
	}

	/// Espera a una posicion concreta de la linea (V y H).
	///
	/// Usa la mascara `0xfffe`: compara tambien los bits horizontales, lo que permite
	/// "copper bars" que cambian un registro a mitad de scanline. `hpos` debe ser par
	/// (los ejemplos clasicos usan 1 con la mascara estandar; para H real se codifica
	/// el valor par que compara).
	void wait_position(u8 vpos, u8 hpos) {
		write_pair(wait_word(vpos, static_cast<u8>(hpos & 0xfe)), 0xfffe);
	}

	/// Finaliza la lista. El Copper se detiene en este par especial.
	void end() {
		write_pair(0xffff, 0xfffe);
		m_overflow_sent = false;
	}

	// --- Extensiones para efectos tipo "copper chunky" (p. ej. plasma) ----------

	/// Emite un MOVE y devuelve el **indice en words de la instruccion** (word0), para
	/// poder parchear luego su word de valor con `patch_data` (equivale a guardar el
	/// `CopInsT*` del original y hacer `CopSetColor` por frame).
	u16 move_at(u16 custom_register_offset, u16 value) {
		const u16 index = m_used_words;
		write_pair(custom_register_offset, value);
		return index;
	}
	u16 move_at(Register reg, u16 value) { return move_at(static_cast<u16>(reg), value); }

	/// Parchea el word de valor de un MOVE emitido antes (indice devuelto por `move_at`).
	void patch_data(u16 instruction_word, u16 value) {
		if (m_ok && (instruction_word + 1u) < m_used_words) {
			m_words[instruction_word + 1u] = value;
		}
	}

	/// SKIP: WAIT con mascara `0xffff` (bit 0 = 1 => salta la instruccion siguiente si el
	/// beam ya paso por (vpos, hpos)). Codificacion de `CopSkip` de libgfx (verbatim).
	/// `hpos` en unidades de color-clock (se divide por 2 como en el original).
	u16 skip(u16 vpos, u16 hpos) {
		const u16 index = m_used_words;
		write_pair(wait_word(static_cast<u8>(vpos & 0xffu), static_cast<u8>((hpos >> 1) | 1u)), 0xffff);
		return index;
	}

	/// WAIT crudo (equivale a `CopInsWait`): `word0 = (vp&0xff)<<8 | ((hp>>1)|1)`,
	/// `word1 = mask`. `hp` en color-clock. Devuelve el indice.
	u16 wait_raw(u16 vp, u16 hp, u16 mask) {
		const u16 index = m_used_words;
		write_pair(static_cast<u16>(((vp & 0xffu) << 8) | (((hp >> 1) | 1u) & 0xffu)), mask);
		return index;
	}

	/// WAIT con mascaras de V/H (equivale a `CopWaitMask`): `word0 = (vp&0xff)<<8 |
	/// ((hp>>1)|1)`, `word1 = ((0x80|vpmask)<<8) | ((hpmask>>1)&0xfe)`.
	u16 wait_masked(u16 vp, u16 hp, u16 vpmask, u16 hpmask) {
		const u16 index = m_used_words;
		write_pair(static_cast<u16>(((vp & 0xffu) << 8) | (((hp >> 1) | 1u) & 0xffu)),
			   static_cast<u16>((((0x80u | (vpmask & 0x7fu)) & 0xffu) << 8) | ((hpmask >> 1) & 0xfeu)));
		return index;
	}

	/// MOVE de 32 bits (puntero) con el **orden del original** (libgfx `CopMove32`):
	/// primero `reg+2` (word bajo) y luego `reg` (word alto). Devuelve el indice.
	u16 move32(Register reg, const void* address) {
		const uintptr raw = reinterpret_cast<uintptr>(address);
		const u16 index = m_used_words;
		write_pair(static_cast<u16>(static_cast<u16>(reg) + 2u), static_cast<u16>(raw & 0xffffu));
		write_pair(static_cast<u16>(reg), static_cast<u16>(raw >> 16));
		return index;
	}

	/// Parchea un MOVE32 emitido con `move32` (indice) con otra direccion (equivale a
	/// `CopInsSet32` del original; p. ej. apuntar `COP2LC` al label de una fila).
	void patch_move32(u16 instruction_word, const void* address) {
		const uintptr raw = reinterpret_cast<uintptr>(address);
		patch_data(instruction_word, static_cast<u16>(raw & 0xffffu));
		if (m_ok && (instruction_word + 3u) < m_used_words) {
			m_words[instruction_word + 3u] = static_cast<u16>(raw >> 16);
		}
	}

	/// Word de la direccion de una instruccion (para calcular labels de copper).
	constexpr const void* instruction_address(u16 instruction_word) const {
		return reinterpret_cast<const void*>(reinterpret_cast<uintptr>(m_words) + instruction_word * 2u);
	}

	constexpr bool ok() const {
		return m_ok;
	}

	constexpr u16* data() const {
		return m_words;
	}

	constexpr u16 words_used() const {
		return m_used_words;
	}

	constexpr u32 bytes_used() const {
		return static_cast<u32>(m_used_words) * sizeof(u16);
	}

private:
	void write_pair(u16 a, u16 b) {
		if (!m_ok || m_used_words + 2 > m_capacity_words) {
			m_ok = false;
			return;
		}

		m_words[m_used_words++] = a;
		m_words[m_used_words++] = b;
	}

	u16* m_words = nullptr;
	u16 m_capacity_words = 0;
	u16 m_used_words = 0;
	bool m_ok = false;
	bool m_overflow_sent = false;
};

} // namespace eng::copper
