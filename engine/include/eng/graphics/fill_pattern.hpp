#pragma once

/// \file fill_pattern.hpp
/// **Patrones de relleno** para el relleno de polígonos por bitplane: tablero de ajedrez,
/// punteados, líneas verticales/horizontales y oblicuas. El relleno por Blitter
/// (`fill_polygons_by_plane`) escribe máscaras de 1 bit por plano; el patrón se aplica
/// **después** del fill con un blit por plano (`D = A & D`: A = plano de patrón, B = D =
/// plano destino, minterm `$C0`), de modo que no cambia el contorno ni la paridad.
///
/// Motivación de coste (por qué un segundo blit y no dentro del fill): el area fill del
/// Blitter solo puede escribir el valor que resulta del minterm + el biestable de relleno
/// (OR/XOR de 1s en el interior); no puede producir 0s *dentro* del polígono, que es lo que
/// pide un patrón (AHRM 3.ª cap. 6 «Area Fill Mode»; semántica exacta en WinUAE-DBG
/// `blitter.cpp:262-286` `build_blitfilltable`). El blit de aplicación cuesta 6
/// ciclos/palabra (A+B+D) sobre la misma caja del fill.
///
/// El **plano de patrón** es un bitmap de 1 bit con el mismo `row_bytes` que el destino y
/// las filas del patrón; se construye UNA vez (`fill_pattern_plane_cpu`, p. ej. al iniciar
/// la escena) y se reutiliza en todos los frames. El canal A avanza en paralelo a D (mismos
/// `row_bytes`), así que el patrón queda anclado a la fila/columna 0 del buffer: llamar con
/// `wx0` alineado a palabra para conservar la fase.
///
/// Restricción de fase horizontal: el patrón se repite cada 16 px (una palabra); los
/// patrones con periodo vertical libre (`Oblique45`/`135`) lo repiten cada 16 filas por
/// aritmética de rotación de la palabra. Es exactamente el formato que encaja con el Blitter.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Catálogo de patrones. `Solid` (todo 1s) equivale a no aplicar patrón.
enum class FillPattern : eng::u8 {
	Solid = 0,
	Checker2,   ///< tablero 2×2 (píxel alterno)
	Checker4,   ///< tablero 4×4
	Dots2,      ///< punteado: 1 píxel cada 2 filas y 4 columnas
	VLines4,    ///< líneas verticales de 1 px cada 4 columnas
	HLines4,    ///< líneas horizontales de 1 fila cada 4
	Oblique45,  ///< líneas a 45° (bajan hacia la derecha), 1 px cada 16 columnas
	Oblique135, ///< líneas a 45° (bajan hacia la izquierda)
};

/// Palabra de 16 px del patrón en la fila `y` (**MSB = píxel izquierdo**, como el resto del
/// engine); se repite a lo ancho con periodo 16 px.
[[nodiscard]] constexpr eng::u16 fill_pattern_word(FillPattern kind, eng::u32 y) {
	switch (kind) {
	case FillPattern::Solid:
		return 0xffffu;
	case FillPattern::Checker2:
		return (y & 1u) != 0u ? 0x5555u : 0xaaaau;
	case FillPattern::Checker4:
		return (y & 2u) != 0u ? 0x3333u : 0xccccu;
	case FillPattern::Dots2:
		return (y & 1u) == 0u ? 0x8888u : 0x0000u;
	case FillPattern::VLines4:
		return 0x8888u;
	case FillPattern::HLines4:
		return (y & 3u) == 0u ? 0xffffu : 0x0000u;
	case FillPattern::Oblique45:
		return static_cast<eng::u16>(0x8000u >> (y & 15u));
	case FillPattern::Oblique135:
		return static_cast<eng::u16>(0x0001u << (y & 15u));
	}
	return 0xffffu;
}

/// Periodo vertical (filas) tras el que la palabra del patrón se repite.
[[nodiscard]] constexpr eng::u8 fill_pattern_period(FillPattern kind) {
	switch (kind) {
	case FillPattern::Solid:
	case FillPattern::VLines4:
		return 1u;
	case FillPattern::Checker2:
	case FillPattern::Dots2:
		return 2u;
	case FillPattern::Checker4:
	case FillPattern::HLines4:
		return 4u;
	case FillPattern::Oblique45:
	case FillPattern::Oblique135:
		return 16u;
	}
	return 1u;
}

/// Construye en CPU el **plano de patrón** (`row_bytes` bytes por fila, `height` filas):
/// cada fila queda como la palabra de `fill_pattern_word` repetida a lo ancho. El llamador
/// lo aloja en Chip RAM (lo lee el Blitter por el canal A) y lo construye una sola vez.
inline void fill_pattern_plane_cpu(eng::MaskBuffer plane, eng::u16 row_bytes, eng::u16 height,
				   FillPattern kind) {
	if (plane.data() == nullptr || row_bytes < 2u || row_bytes > plane.size()) {
		return;
	}
	const eng::u32 words = static_cast<eng::u32>(row_bytes) / 2u;
	const eng::u32 rows_max = static_cast<eng::u32>(plane.size() / row_bytes);
	const eng::u32 rows = height < rows_max ? height : rows_max;
	eng::u16* base = reinterpret_cast<eng::u16*>(plane.data());
	for (eng::u32 y = 0; y < rows; ++y) {
		const eng::u16 w = fill_pattern_word(kind, y);
		eng::u16* row = base + y * words;
		for (eng::u32 i = 0; i < words; ++i) {
			row[i] = w;
		}
	}
}

} // namespace eng::graphics
