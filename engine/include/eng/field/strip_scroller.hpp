#pragma once

/// \file strip_scroller.hpp
/// **Scroller de tiras** (anillo de Copper + tira entrante) — camino rápido de scroll
/// (referencia: `docs/debugging/investigaciones/consulta-scroll-optimizacion*.md`; test HOST-244).
///
/// Geometría compile-time y **planificador por frame**: dado el scroll actual y el previo decide
/// qué columna/fila entrante hay que pintar (una tira) y los valores de Copper a parchear
/// (`BPLxPT` + `BPLCON1` + anillo vertical), **sin re-emitir la lista**. Es el algoritmo (host
/// testeable); el backend ejecuta el/los blit(s) de la tira y parchea la copperlist.
///
/// Modos con **Copper split** asumen `viewport_h + 2*tile_h <= 208` (VPOS de 8 bits en OCS;
/// `0x2c + 208 = 252 <= 255`), evitando duplicar el buffer (espejo/lineal). Ver `SCROLL_VARIANTS.md` §3.4.

#include <eng/core/types/types.hpp>

namespace eng::field {

/// Geometría del anillo de tiras. Todos los parámetros son compile-time (NTTP).
template <eng::u16 ViewportW = 320u, eng::u16 ViewportH = 208u, eng::u8 Planes = 5u,
	  eng::u16 TileW = 16u, eng::u16 TileH = 16u, eng::u16 GuardWords = 2u,
	  eng::u16 FetchExtraWords = 1u, bool SplitVertical = false>
struct StripScrollGeometry {
	static_assert(TileW == 16u || TileW == 32u, "tile 16 o 32");
	static_assert(TileH == 16u || TileH == 32u, "tile 16 o 32");
	static_assert(ViewportW % TileW == 0u, "viewport_w multiplo de tile");
	static_assert(ViewportH % TileH == 0u, "viewport_h multiplo de tile");
	/// Guarda: cubre el paso maximo (<= 16 px = 1 palabra) mas la palabra en vuelo.
	static_assert(GuardWords >= 2u, "guarda >= ceil(step_max/16)+1 = 2");
	/// Split de Copper: el WAIT cae en `0x2c + viewport_h` y debe caber en VPOS (8 bits, 0..255).
	/// `viewport_h <= 208` -> `0x2c + 208 = 252 <= 255` (y no hace falta duplicar el buffer).
	static_assert(!SplitVertical || (0x2cu + ViewportH) <= 255u,
		      "split OCS: 0x2c + viewport_h <= 255 (viewport_h <= 208)");

	// Parametros expuestos (los NTTP no son accesibles como `Geom::X`).
	static constexpr eng::u16 viewport_w = ViewportW;
	static constexpr eng::u16 viewport_h = ViewportH;
	static constexpr eng::u8 planes = Planes;
	static constexpr eng::u16 tile_w = TileW;
	static constexpr eng::u16 tile_h = TileH;
	static constexpr bool split_vertical = SplitVertical;

	static constexpr eng::u16 visible_words = static_cast<eng::u16>(ViewportW / 16u);	static constexpr eng::u16 ring_w_words =
		static_cast<eng::u16>(visible_words + GuardWords + FetchExtraWords);
	static constexpr eng::u16 ring_w_bytes = static_cast<eng::u16>(ring_w_words * 2u);
	/// Salto de fila del display interleaved: (PLANES-1) planos por delante.
	static constexpr eng::u16 bpl_mod = static_cast<eng::u16>((Planes - 1u) * ring_w_bytes);
	/// Ancho de la tira en palabras (tile 16 -> 1, tile 32 -> 2).
	static constexpr eng::u16 strip_words = static_cast<eng::u16>(TileW / 16u);
	/// `BLTDMOD` de la tira en el anillo interleaved.
	static constexpr eng::u16 bltdmod_col =
		static_cast<eng::u16>(ring_w_bytes - strip_words * 2u);
	/// Planelíneas de una columna completa (viewport entero x planos).
	static constexpr eng::u16 column_planelines = static_cast<eng::u16>(ViewportH * Planes);
	/// Tiles que forman una columna/fila completa.
	static constexpr eng::u16 column_tiles = static_cast<eng::u16>(ViewportH / TileH);
	static constexpr eng::u16 row_tiles = static_cast<eng::u16>(ViewportW / TileW);
	/// Anillo vertical (solo si hay split): alto del bitmap.
	static constexpr eng::u16 ring_h = static_cast<eng::u16>(ViewportH + (SplitVertical ? 2u * TileH : 0u));
};

/// Decisión de un frame del scroller de tiras (lo que el backend debe ejecutar/parchear).
struct StripFrame {
	bool column_crossed = false;   ///< cruce de frontera de tile en X -> pintar la columna entrante
	eng::u16 col_dest_word = 0;     ///< palabra del anillo (destino de la tira), FUERA de la ventana
	bool row_crossed = false;       ///< cruce en Y -> pintar la fila entrante (solo si SplitVertical)
	eng::u16 row_dest_line = 0;     ///< linea del anillo vertical
	eng::u16 window_word = 0;       ///< base de la ventana visible (BPLxPT), palabra del anillo
	eng::u8 bplcon1_fine = 0;       ///< fine scroll (BPLCON1), 0..15
	eng::u8 blits = 0;              ///< tiras a pintar este frame (column + row, <= 2)
};

/// **Planificador**: dado el scroll actual `(sx, sy)` y el previo `(psx, psy)` (en px, no negativos),
/// decide las tiras a pintar y los valores de Copper. El orden real es: pintar la tira (destination
/// en la guarda, invisible) -> parchear Copper -> el haz muestra la ventana nueva.
template <class Geom>
[[nodiscard]] constexpr StripFrame plan_strip_frame(eng::s32 sx, eng::s32 sy, eng::s32 psx,
						    eng::s32 psy) noexcept {
	StripFrame f {};
	f.window_word = static_cast<eng::u16>((static_cast<eng::u32>(sx) / 16u) % Geom::ring_w_words);
	f.bplcon1_fine = static_cast<eng::u8>(static_cast<eng::u32>(sx) & 15u);
	// Columna entrante: la palabra que se revela al avanzar de `psx` a `sx`. Se pinta cuando la
	// camara (en el frame anterior) estaba en `psx`, en el borde que la ventana revelara.
	if ((sx / 16) != (psx / 16)) {
		f.column_crossed = true;
		const eng::u16 pw = static_cast<eng::u16>((static_cast<eng::u32>(psx) / 16u) % Geom::ring_w_words);
		f.col_dest_word = (sx > psx)
			? static_cast<eng::u16>((pw + Geom::visible_words) % Geom::ring_w_words)
			: static_cast<eng::u16>((pw + Geom::ring_w_words - 1u) % Geom::ring_w_words);
		++f.blits;
	}
	if constexpr (Geom::split_vertical) {
		if ((sy / Geom::tile_h) != (psy / Geom::tile_h)) {
			f.row_crossed = true;
			f.row_dest_line = static_cast<eng::u16>((static_cast<eng::u32>(psy) / Geom::tile_h) %
								(Geom::ring_h / Geom::tile_h));
			++f.blits;
		}
	}
	return f;
}

/// Predicado de verificación (HOST-244): `dest` no debe caer dentro de la ventana visible.
[[nodiscard]] constexpr bool strip_dest_is_guard(eng::u16 dest, eng::u16 window_word,
						 eng::u16 visible_words, eng::u16 ring_w_words) noexcept {
	for (eng::u16 k = 0u; k < visible_words; ++k) {
		if (dest == static_cast<eng::u16>((window_word + k) % ring_w_words)) return false;
	}
	return true;
}

} // namespace eng::field
