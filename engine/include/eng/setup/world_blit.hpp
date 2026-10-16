#pragma once

/// \file world_blit.hpp
/// **Validadores de geometría de blits de mundo** (`eng::setup`): comprueban en setup la
/// geometría con la que una demo dibuja objetos en el **anillo** de un playfield corkscrew
/// (X-Limited y afines) — alineación a palabra, columna dentro de la fila, alto dentro del
/// bucle del anillo, cobertura del span fuente y ventanas de restauración dentro de la
/// banda. Son funciones **puras** (`constexpr`, sin hardware) como las de `coherence.hpp`.
///
/// Nacen de un fallo real de la demo 121: los blits del actor sumaban una guarda de anillo
/// (`display_offset`) que la sonda de lectura no conocía, y el objeto se dibujaba/leía en
/// filas distintas — un desajuste **silencioso** que la validación de setup convierte en
/// un `CheckResult` con el valor culpable. Regla de oro: la fila del anillo de una fila
/// visible es `ring_line_for_visible(display_offset, y, display_height)`; úsala en lugar de
/// constantes propias.
///
/// Documentación del sistema de capas: `docs/engine/architecture/COHERENCIA_DE_SETUP.md`.

#include <eng/core/types/types.hpp>
#include <eng/setup/coherence.hpp>

namespace eng::setup {

/// **Línea del anillo** de una fila VISIBLE del corkscrew: la ventana visible empieza en
/// `display_offset` y el anillo envuelve en `display_height`.
/// \param display_offset origen de la ventana en el anillo (filas).
/// \param y_visible fila visible (0..viewport_h-1).
/// \param display_height vueltas del anillo (0 → sin wrap, devuelve `display_offset`).
/// \return línea del anillo para los blits de mundo.
[[nodiscard]] constexpr eng::u16 ring_line_for_visible(eng::u16 display_offset, eng::u16 y_visible,
						       eng::u16 display_height) {
	if (display_height == 0u) {
		return display_offset;
	}
	return eng::u16((eng::u32(display_offset) + eng::u32(y_visible)) % eng::u32(display_height));
}

/// Geometría de un **blit de mundo** (rect de `planes` planos con origen en una banda
/// planar `[plano][fila][palabra]`) para `check_world_blit`.
struct WorldBlitGeometry {
	eng::u16 x = 0u;                  ///< x de destino en píxeles (múltiplo de 16)
	eng::u16 w = 0u;                  ///< ancho en píxeles (múltiplo de 16)
	eng::u16 h = 0u;                  ///< filas
	eng::u8 planes = 0u;              ///< planos del blit
	eng::u16 row_bytes = 0u;          ///< bytes por fila de la fuente (misma geometría que el destino)
	eng::u32 plane_stride_bytes = 0u; ///< separación entre planos de la fuente
	eng::u32 span_words = 0u;         ///< palabras disponibles del span fuente

	/// Palabras mínimas que el emisor necesita leer del span (misma fórmula que el
	/// emisor de blits de mundo: planos por stride + filas por fila + ancho).
	[[nodiscard]] constexpr eng::u32 need_words() const {
		const eng::u32 words = eng::u32(w) / 16u;
		return (planes > 1u ? eng::u32(planes - 1u) * (plane_stride_bytes / 2u) : 0u) +
		       (h > 1u ? eng::u32(h - 1u) * (eng::u32(row_bytes) / 2u) : 0u) + words;
	}
};

/// Valida la geometría de un blit de mundo contra la superficie destino.
/// \param g geometría del blit. \param row_pixels ancho de la fila destino en píxeles
///        (`bitmap_bytes_per_row · 8`; 0 = sin chequeo). \param display_height bucle
///        vertical del anillo (0 = sin chequeo).
/// \return `CheckResult` (códigos: 1 x desalineado · 2 ancho no múltiplo de 16 · 3 alto
///         fuera del bucle · 4 excede la fila destino · 5 span insuficiente).
[[nodiscard]] constexpr CheckResult check_world_blit(const WorldBlitGeometry& g,
						     eng::u16 row_pixels = 0u,
						     eng::u16 display_height = 0u) {
	if ((g.x & 15u) != 0u) {
		return {1u, "x de blit de mundo desalineado a 16", 0u, 0u, g.x};
	}
	if (g.w == 0u || (g.w & 15u) != 0u) {
		return {2u, "ancho de blit de mundo no multiplo de 16", 0u, 0u, g.w};
	}
	if (g.h == 0u || (display_height != 0u && g.h > display_height)) {
		return {3u, "alto de blit de mundo fuera del bucle del anillo", 0u, display_height,
			g.h};
	}
	if (row_pixels != 0u && eng::u32(g.x) + eng::u32(g.w) > eng::u32(row_pixels)) {
		return {4u, "blit de mundo excede la fila destino", 0u, row_pixels,
			eng::u32(g.x) + eng::u32(g.w)};
	}
	if (g.span_words != 0u && g.span_words < g.need_words()) {
		return {5u, "span de la fuente no cubre el blit de mundo", 0u, g.need_words(),
			g.span_words};
	}
	return {};
}

/// Valida una **ventana de restauración** dentro de una banda planar completa
/// (`[plano][fila][band_words_per_row]`): `first_word` (columna en palabras) + `w_words`
/// no pueden salirse de la fila.
/// \param first_word columna inicial en palabras (x/16 de la ventana).
/// \param w_words ancho de la ventana en palabras.
/// \param band_words_per_row palabras por fila de la banda.
/// \return `CheckResult` (código 6: ventana vacía o fuera de la fila).
[[nodiscard]] constexpr CheckResult check_world_window(eng::u16 first_word, eng::u16 w_words,
						       eng::u16 band_words_per_row) {
	if (w_words == 0u || band_words_per_row == 0u ||
	    eng::u32(first_word) + eng::u32(w_words) > eng::u32(band_words_per_row)) {
		return {6u, "ventana de mundo vacia o fuera de la fila de la banda", 0u,
			band_words_per_row, eng::u32(first_word) + eng::u32(w_words)};
	}
	return {};
}

} // namespace eng::setup
