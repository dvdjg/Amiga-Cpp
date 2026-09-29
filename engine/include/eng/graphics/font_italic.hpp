#pragma once

/// \file font_italic.hpp
/// **Variantes derivadas de las fuentes** (`eng`): cursiva (*italic*) y micro-fuente, obtenidas
/// por **transformación** de `Font8`/`Font5x7` sin duplicar tablas. Completa la colección de
/// glifos del engine: `Font8` (8×8) y `Font5x7` (5×7, HUD) tienen su versión cursiva, y hay una
/// **micro-fuente** `Font3x5` derivada de `Font5x7` para etiquetas muy pequeñas.
///
/// Cursiva: *shear* horizontal progresivo por fila (la fila `r` se desplaza `slant(rows, r)` px a
/// la derecha), la aproximación clásica de itálica en fuentes bitmap. No cambia el avance (8 px en
/// `Font8`, 5 en `Font5x7`): el glifo sigue cabiendo en su celda si se reserva `slant` px.
///
/// Micro-fuente: `Font3x5` se deriva de `Font5x7` tomando 3 de sus 5 columnas y 5 de sus 7 filas
/// (submuestreo determinista), suficiente para micro-etiquetas legibles. Ver
/// `docs/engine/architecture/GUI_LIBRARY.md` §6.

#include <eng/core/types/types.hpp>
#include <eng/graphics/font5x7.hpp>
#include <eng/graphics/font8.hpp>

namespace eng {

/// Desplazamiento horizontal (px) de la fila `r` de `rows` con inclinación máxima `max_slant`.
/// 0 en la fila superior, `max_slant` en la inferior. Constante de compilación.
[[nodiscard]] constexpr eng::u8 italic_shift(eng::u8 r, eng::u8 rows, eng::u8 max_slant) noexcept {
	if (rows <= 1u || max_slant == 0u) {
		return 0u;
	}
	return static_cast<eng::u8>(static_cast<eng::u16>(r) * max_slant / (rows - 1u));
}

/// Fila de `Font8` en cursiva: `row_bits` (bit k = columna k, 0=izq) desplazada `d` px a la
/// derecha dentro de los 8 px del glifo. Los bits que salen por la derecha se descartan.
[[nodiscard]] constexpr eng::u8 font8_italic_row(eng::u8 row_bits, eng::u8 d) noexcept {
	if (d == 0u) {
		return row_bits;
	}
	return static_cast<eng::u8>((row_bits << d) & 0xffu);
}

/// Fila de `Font5x7` en cursiva: `row_bits` (bit 4 = columna 0, 0=izq) desplazada `d` px a la
/// derecha dentro de los 5 px. Los bits que salen por la derecha se descartan.
[[nodiscard]] constexpr eng::u8 font5x7_italic_row(eng::u8 row_bits, eng::u8 d) noexcept {
	if (d == 0u) {
		return row_bits;
	}
	return static_cast<eng::u8>((row_bits >> d) & 0x1fu);
}

/// Fila de `Font8` en cursiva para el glifo `ch`, fila `r`.
[[nodiscard]] constexpr eng::u8 font8_row_italic(eng::u16 ch, eng::u8 r) noexcept {
	return font8_italic_row(eng::Font8::row(ch, r), italic_shift(r, eng::Font8::kRows, 2u));
}

/// Fila de `Font5x7` en cursiva para el glifo `ch`, fila `r`.
[[nodiscard]] constexpr eng::u8 font5x7_row_italic(eng::u16 ch, eng::u8 r) noexcept {
	return font5x7_italic_row(eng::Font5x7::row(ch, r), italic_shift(r, eng::Font5x7::kRows, 2u));
}

/// **Micro-fuente 3×5** derivada de `Font5x7`: toma las columnas 1..3 (de 5) y las filas 1..5
/// (de 7). Sin tabla propia: se lee de `Font5x7`. `ch` ASCII/LATIN-1 soportado por `Font5x7`.
struct Font3x5 {
	static constexpr eng::u8 kRows = 5;

	/// Fila `r` (0..4) del glifo `ch`: filas 1..5 de `Font5x7`, bits 1..3 (columna `4-k`) → bit
	/// `2-k` (MSB-izquierda en 3 bits, para un glifo de 3 px de ancho).
	[[nodiscard]] static constexpr eng::u8 row(eng::u16 ch, eng::u8 r) noexcept {
		const eng::u8 src = eng::Font5x7::row(ch, static_cast<eng::u8>(r + 1u));
		// Fuente 5x7: bit4=col0. Tomamos cols 0..2 -> bits 4..2 -> 3 bits (bit2=col0).
		return static_cast<eng::u8>((src >> 2u) & 0x07u);
	}

	/// Fila `r` del glifo `ch` en cursiva (shear de 1 px).
	[[nodiscard]] static constexpr eng::u8 row_italic(eng::u16 ch, eng::u8 r) noexcept {
		const eng::u8 bits = row(ch, r);
		const eng::u8 d = italic_shift(r, kRows, 1u);
		return static_cast<eng::u8>((bits << d) & 0x07u);
	}
};

} // namespace eng
