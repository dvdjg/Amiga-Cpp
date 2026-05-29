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

#include <amg/core/types.hpp>
#include <amg/memory/arena.hpp>

namespace amg::copper {

/// Offsets de registros custom usados por las primeras demos.
///
/// Los valores son offsets desde la base custom `$dff000`. Coinciden con los campos
/// del Hardware Reference Manual y con `offsetof(struct Custom, campo)`.
enum class Register : u16 {
	COP1LCH = 0x080,
	COP1LCL = 0x082,
	COPJMP1 = 0x088,
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
		const u32 raw = reinterpret_cast<u32>(address);
		move(bitplane_pointer_high_register(plane), static_cast<u16>(raw >> 16));
		move(bitplane_pointer_low_register(plane), static_cast<u16>(raw & 0xffffu));
	}

	/// Espera a una linea de raster con mascara estandar.
	void wait_line(u8 vpos) {
		write_pair(wait_word(vpos), 0xff00);
	}

	/// Finaliza la lista. El Copper se detiene en este par especial.
	void end() {
		write_pair(0xffff, 0xfffe);
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
};

} // namespace amg::copper
