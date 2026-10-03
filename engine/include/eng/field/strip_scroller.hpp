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
#include <eng/graphics/blitter_state.hpp>

namespace eng::field {

/// Geometría del anillo de tiras. Todos los parámetros son compile-time (NTTP).
template <eng::u16 ViewportW = 320u, eng::u16 ViewportH = 208u, eng::u8 Planes = 5u,
	  eng::u16 TileW = 16u, eng::u16 TileH = 16u, eng::u16 GuardWords = 2u,
	  eng::u16 FetchExtraWords = 1u, bool SplitVertical = false, eng::u16 RingWords = 0u>
struct StripScrollGeometry {
	static_assert(TileW == 16u || TileW == 32u, "tile 16 o 32");
	static_assert(TileH == 16u || TileH == 32u, "tile 16 o 32");
	static_assert(ViewportW % TileW == 0u, "viewport_w multiplo de tile");
	static_assert(ViewportH % TileH == 0u, "viewport_h multiplo de tile");
	/// Guarda: cubre el paso maximo (<= 16 px = 1 palabra) mas la palabra en vuelo.
	static_assert(GuardWords >= 2u, "guarda >= ceil(step_max/16)+1 = 2");
	/// Split de Copper: la linea de split es `0x2c + viewport_h`. Con la secuencia **two-WAIT**
	/// (`$ffdf,$fffe` + `$0001,$fffe`) cruza la linea 255, asi que un display de 256 lineas SI se
	/// puede partir; el cap `viewport_h <= 208` (`0x2c+208=252`) es la alternativa de un solo WAIT.
	static_assert(!SplitVertical || ViewportH <= 256u, "split: viewport <= 256 (two-WAIT)");

	// Parametros expuestos (los NTTP no son accesibles como `Geom::X`).
	static constexpr eng::u16 viewport_w = ViewportW;
	static constexpr eng::u16 viewport_h = ViewportH;
	static constexpr eng::u8 planes = Planes;
	static constexpr eng::u16 tile_w = TileW;
	static constexpr eng::u16 tile_h = TileH;
	static constexpr bool split_vertical = SplitVertical;
	/// Linea del split (`0x2c + viewport_h`); si `> 255` hace falta la secuencia two-WAIT.
	static constexpr eng::u16 split_line = static_cast<eng::u16>(0x2cu + ViewportH);
	static constexpr bool split_crosses_255 = split_line > 255u;
	static constexpr eng::u16 visible_words = static_cast<eng::u16>(ViewportW / 16u);
	/// Ancho del anillo en words. `RingWords == 0` = **pantalla + guarda + fetch** (fondo que se
	/// repite); para un mapa largo el anillo es el ancho del bitmap del mapa.
	static constexpr eng::u16 ring_w_words =
		(RingWords != 0u) ? RingWords
				  : static_cast<eng::u16>(visible_words + GuardWords + FetchExtraWords);
	static_assert(ring_w_words >= visible_words + GuardWords + FetchExtraWords,
		      "el anillo debe caber al menos pantalla + guarda + fetch");
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
	/// `BLTSIZE` tiene H de 10 bits (max 1024 planelíneas) -> la columna se parte en varios blits.
	static constexpr eng::u16 max_blt_h = 1024u;
	static constexpr eng::u8 column_blits =
		static_cast<eng::u8>((column_planelines + max_blt_h - 1u) / max_blt_h);
	static_assert(column_blits >= 1u, "columna necesita al menos 1 blit");
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
	// El puntero de la ventana NO puede envolver el anillo (el display lee la ventana CONTIGUA, sin
	// wrap): recorre `[0, ring_w_words - visible_words]` y envuelve ahi. Con un anillo de 2 pantallas
	// (+ guarda) la ventana siempre cabe y las columnas entrantes se pintan en la 2a pantalla.
	const eng::u16 span = static_cast<eng::u16>(Geom::ring_w_words - Geom::visible_words);
	f.window_word = static_cast<eng::u16>((static_cast<eng::u32>(sx) / 16u) % span);
	f.bplcon1_fine = static_cast<eng::u8>(static_cast<eng::u32>(sx) & 15u);
	// Columna entrante: la palabra que se revela al avanzar de `psx` a `sx`. Se pinta cuando la
	// camara (en el frame anterior) estaba en `psx`, en el borde que la ventana revelara.
	if ((sx / 16) != (psx / 16)) {
		f.column_crossed = true;
		const eng::u16 pw = static_cast<eng::u16>((static_cast<eng::u32>(psx) / 16u) % span);
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

/// Valores a escribir en la copperlist por frame (sin re-emitir): fine scroll, la direccion
/// (offset de byte desde la base del anillo) de cada `BPLxPT`, y la linea de split (con two-WAIT
/// si cruza la 255).
struct StripCopper {
	eng::u16 bplcon1 = 0u;        ///< valor de BPLCON1 (fine scroll 0..15)
	eng::u32 pt_byte[8] {};       ///< offset de cada `BPLxPT` desde la base del anillo (un anillo continuo)
	eng::u8 planes = 0u;
	eng::u16 split_line = 0u;     ///< linea del split (0 = sin split)
	bool split_two_wait = false;  ///< true si el split cruza la linea 255 (secuencia two-WAIT)
};

/// Calcula los valores de Copper a parchear a partir del plan de frame. El anillo es **continuo**:
/// el plano `p` vive `p*ring_w_bytes` por delante, y la ventana empieza en `window_word*2` bytes.
template <class Geom>
[[nodiscard]] constexpr StripCopper strip_copper_values(const StripFrame& f) noexcept {
	StripCopper c {};
	c.bplcon1 = f.bplcon1_fine;
	c.planes = Geom::planes;
	for (eng::u8 p = 0u; p < Geom::planes; ++p) {
		c.pt_byte[p] = static_cast<eng::u32>(p) * Geom::ring_w_bytes +
			       static_cast<eng::u32>(f.window_word) * 2u;
	}
	if constexpr (Geom::split_vertical) {
		c.split_line = Geom::split_line;
		c.split_two_wait = Geom::split_crosses_255;
	}
	return c;
}

/// Predicado de verificación (HOST-244): `dest` no debe caer dentro de la ventana visible.
[[nodiscard]] constexpr bool strip_dest_is_guard(eng::u16 dest, eng::u16 window_word,
						 eng::u16 visible_words, eng::u16 ring_w_words) noexcept {
	for (eng::u16 k = 0u; k < visible_words; ++k) {
		if (dest == static_cast<eng::u16>((window_word + k) % ring_w_words)) return false;
	}
	return true;
}

/// Descriptor del blit de la tira (registros exactos). La columna se parte en `Geom::column_blits`
/// blits de `<= 1024` planelíneas (el campo H de `BLTSIZE` es de 10 bits). `ash` = fine shift 0..15
/// (`BLTCON0` bits 15..12). Origen = columna pre-compuesta (contigua, `BLTAMOD=0`); destino = anillo
/// (interleaved, `BLTDMOD = ring_w_bytes - strip_words*2`).
struct StripBlit {
	eng::u16 bltcon0 = 0u;
	eng::u16 bltcon1 = 0u;
	eng::u16 bltafwm = 0xffffu;
	eng::u16 bltalwm = 0xffffu;
	eng::s16 bltamod = 0;
	eng::s16 bltdmod = 0;
	eng::u16 bltsize = 0u;
};

template <class Geom>
[[nodiscard]] constexpr StripBlit strip_blit_desc(eng::u8 ash, eng::u8 chunk = 0u) noexcept {
	StripBlit b {};
	b.bltcon0 = static_cast<eng::u16>(eng::graphics::kBlitterUseA | eng::graphics::kBlitterUseD |
					  eng::graphics::kBlitterMintermCopyA |
					  (static_cast<eng::u16>(ash) << 12u));
	b.bltcon1 = 0u;
	b.bltafwm = 0xffffu;
	b.bltalwm = 0xffffu;
	b.bltamod = 0;
	b.bltdmod = Geom::bltdmod_col;
	const eng::u16 start = static_cast<eng::u16>(chunk) * Geom::max_blt_h;
	const eng::u16 remaining = static_cast<eng::u16>(Geom::column_planelines - start);
	const eng::u16 h = remaining < Geom::max_blt_h ? remaining : Geom::max_blt_h;
	b.bltsize = static_cast<eng::u16>((h << 6u) | Geom::strip_words);
	return b;
}

/// **Compone una columna completa** (viewport entero) concatenando los `Geom::column_tiles` tiles del
/// banco — cada uno `tile_h*planes` palabras (interleaved) — en `dst` **contiguo**, listo para el
/// blit con `BLTAMOD=0`. `bank_stride_words` = palabras por tile en el banco. Devuelve las palabras
/// escritas.
///
/// **Los tiles de la guarda vienen de un mapa que referencia al tilemap y NO se asume que estén
/// contiguos en memoria** (su layout no tiene por qué ser el adecuado): por eso se copian **tile a
/// tile**. Si se quisiera un origen unido habría que **pre-procesarlo** (no se puede esperar que
/// muchos tiles caigan juntos). Se llama solo al cruzar tile (0 composiciones en frames sin cruce).
template <class Geom>
[[nodiscard]] constexpr eng::u16 compose_column(eng::u16* dst, const eng::u16* tile_bank,
						const eng::u16* tile_ids,
						eng::u16 bank_stride_words) noexcept {
	const eng::u16 tile_words = static_cast<eng::u16>(Geom::tile_h * Geom::planes);
	eng::u16 w = 0u;
	for (eng::u16 t = 0u; t < Geom::column_tiles; ++t) {
		const eng::u16* src = tile_bank + static_cast<eng::u32>(tile_ids[t]) * bank_stride_words;
		for (eng::u16 i = 0u; i < tile_words; ++i) dst[w++] = src[i];
	}
	return w;
}

} // namespace eng::field
