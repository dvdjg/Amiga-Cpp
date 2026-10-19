#pragma once

/// \file diff_text.hpp
/// \brief Línea de texto de HUD pintada **por diferencias** sobre `Surface::draw_text_words`.
///
/// Repintar una línea entera cada frame cuesta una escritura de palabra por cada par de
/// glifos (ver `Surface::draw_text_words`); con un contador o una etiqueta que cambia cada
/// frame, casi todas esas escrituras son redundantes. `DiffText` guarda la última cadena
/// pintada en la fila y repinta **solo las celdas de 16 px que cambiaron**: típicamente 1-2
/// palabras de un contador frente a ~20 de la línea completa. Es la diferencia entre un
/// frame que cabe en un campo de VBlank y uno que cae a 1/N campos (demo `001_adv_walk`,
/// su `VALIDATION.md`; coste de la palabra medido en HOST-449).
///
/// Modelo de uso: una instancia por fila de HUD (x/y/color fijos en el llamador). El camino
/// de HUD trata los bytes como code points directos (ASCII/LATIN-1), igual que
/// `draw_text_words`; la capacidad es fija en compilación (sin heap, patrón del engine).
///
/// ```text
///   DiffText<64> contador;                  // una fila de HUD
///   contador.paint(hud, 16, 22, "FRM 1234");  // pinta todo la 1.ª vez
///   contador.paint(hud, 16, 22, "FRM 1235");  // repinta SOLO la celda "35" -> "45"...
///   contador.paint(hud, 16, 22, "FRM 1236");  // ...y actualiza prev
/// ```
///
/// **Aviso de composición**: la primera `paint` escribe **todas** sus celdas (las que
/// sobran, con espacios). Si otra escritura comparte fila a la derecha, hay que pintarla
/// **después** de este `DiffText`, o su primera pasada la borrará (bug real de la demo
/// `002_adv_verbs`: el contador de frames borraba la etiqueta de versión de su derecha).

#include <eng/core/types/types.hpp>
#include <eng/field/surface.hpp>

namespace eng::playfield {

/// Escritor de una fila de HUD con repintado por diferencias, capacidad `MaxChars`.
///
/// Contrato:
///   - `paint` escribe con `Surface::draw_text_words`: `x` debe estar alineada a 16 px y
///     la fila completa dentro del clip, o la llamada falla (devuelve `false` y no toca
///     `prev`, de modo que el reintento en la siguiente llamada repinta las celdas
///     pendientes).
///   - Las celdas más allá del NUL lógico se tratan como **espacios**: acortar la cadena
///     borra lo que sobra, y dos cadenas iguales no generan ninguna escritura.
///   - `MaxChars` debe ser par (las celdas son pares de glifos); por defecto 64.
template <eng::u16 MaxChars = 64u>
class DiffText {
	static_assert(MaxChars % 2u == 0u, "DiffText: MaxChars debe ser par (celdas de 2 glifos)");

public:
	/// Pinta `text` en (`x`,`y`) repintando solo las celdas cuyo par de glifos difiere de la
	/// última pintada. Devuelve `false` si `draw_text_words` rechazó una celda (fuera de
	/// clip, `x` sin alinear); en ese caso `prev` queda como estaba y la próxima llamada
	/// reintenta esa celda y las siguientes.
	bool paint(Surface& surf, eng::s32 x, eng::s32 y, const char* text, eng::u8 color = 1u) {
		if (text == nullptr) {
			text = "";
		}
		eng::u16 len = 0u;
		while (text[len] != '\0' && len < MaxChars) {
			++len;
		}
		for (eng::u16 w = 0u; w * 2u < MaxChars; ++w) {
			// Más allá del final de la cadena la celda es un espacio (borra residuos).
			const char c0 = (w * 2u < len) ? text[w * 2u] : ' ';
			const char c1 = (w * 2u + 1u < len) ? text[w * 2u + 1u] : ' ';
			if (m_prev[w * 2u] == c0 && m_prev[w * 2u + 1u] == c1) {
				continue;
			}
			const char cell[3] = {c0, c1, '\0'};
			if (!surf.draw_text_words(x + static_cast<eng::s32>(w) * 16, y, cell, color)) {
				return false;
			}
			m_prev[w * 2u] = c0;
			m_prev[w * 2u + 1u] = c1;
		}
		return true;
	}

	/// Olvida la última cadena pintada: la próxima `paint` repinta toda la fila.
	void reset() {
		for (eng::u16 i = 0u; i < MaxChars; ++i) {
			m_prev[i] = '\0';
		}
	}

private:
	char m_prev[MaxChars] {};  ///< última cadena pintada (celdas de 2 glifos)
};

}  // namespace eng::playfield
