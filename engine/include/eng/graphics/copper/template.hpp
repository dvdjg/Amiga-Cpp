#pragma once

/// \file template.hpp
/// **Plantilla de copperlist**: construye la ESTRUCTURA una vez y expone *slots* para
/// parchear por frame **solo las palabras que cambian**.
///
/// Por que existe: en el A500, escribir la copperlist en chip RAM durante el display de
/// varios planos cuesta ~decenas de ciclos por palabra (la CPU compite por el bus con el
/// DMA de bitplanes). Medido en la demo `125_layers_dualpf` (6 planos): reconstruir la lista
/// entera cada frame (~900 palabras) cuesta ~76k ciclos y baja el frame de 1 a 1.5 campos
/// (49.9 -> 32.5 fps). Con la plantilla se escribe **una vez** la estructura y por frame solo
/// se parchean los datos de MOVE y la linea de WAIT -> muchas menos escrituras.
///
/// No es "compilacion estatica" del efecto (los datos dependen del frame); es separar
/// **estructura** (fija) de **datos** (por frame). Un `constexpr`/expression-template puede
/// generar la estructura sin coste, pero el cuello es el bus, no la CPU: el ahorro real viene
/// de escribir menos palabras.
///
/// Uso (esquema "construir una vez, parchear por frame"):
///
///   copper::Template t { block };
///   u16 c1 = t.move_slot(Register::COLOR01, 0);   // slot del dato
///   u16 w  = t.wait_slot(0);                       // slot de la instruccion WAIT
///   t.end();
///   ... por frame ...
///   t.set(c1, color);        // 1 palabra
///   t.set_wait(w, line);     // 1 palabra

#include <eng/graphics/copper/copper.hpp>

namespace eng::copper {

/// Construye la estructura de una copperlist y guarda los *slots* para el parcheo por frame.
class Template {
public:
	constexpr Template() = default;
	explicit Template(MemoryBlock block) : m_b(block) {}
	explicit Template(eng::Block<eng::CopperTag> block) : m_b(block) {}

	/// Añade un MOVE y devuelve el slot de su **palabra de dato**.
	u16 move_slot(u16 custom_register_offset, u16 value) {
		return m_b.move_at(custom_register_offset, value);
	}
	u16 move_slot(Register reg, u16 value) { return m_b.move_at(reg, value); }

	/// Añade un WAIT (linea 0..255) y devuelve el slot de su **palabra de instruccion**.
	u16 wait_slot(u16 line) {
		const u16 index = m_b.words_used();
		m_b.wait_line(static_cast<u8>(line & 0xffu));
		return index;
	}
	/// WAIT de linea PAL completa (0..311), con el par de overflow de `CopWaitSafe`.
	u16 wait_slot_pal(u16 line) {
		const u16 index = m_b.words_used();
		m_b.wait_line_pal(line);
		return index;
	}

	/// Parchea el dato de un MOVE.
	__attribute__((always_inline)) inline void set(u16 slot, u16 value) {
		m_b.patch_data(slot, value);
	}
	/// Parchea el REGISTRO (destino) de un MOVE: permite que un slot cambie de destino
	/// (p. ej. escribir COLOR01..06 o COLOR09..13 segun la fase).
	__attribute__((always_inline)) inline void set_reg(u16 slot, u16 reg) {
		m_b.patch_move_reg(slot, reg);
	}
	/// Parchea la linea de un WAIT.
	__attribute__((always_inline)) inline void set_wait(u16 slot, u16 line) {
		m_b.patch_wait(slot, line);
	}

	void end() { m_b.end(); }
	[[nodiscard]] bool ok() const { return m_b.ok(); }
	[[nodiscard]] u16* data() const { return m_b.data(); }
	[[nodiscard]] u16 words() const { return m_b.words_used(); }

private:
	ListBuilder m_b;
};

} // namespace eng::copper
