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

#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/blitter_state.hpp>
#include <eng/graphics/playfield_scroll.hpp>

namespace eng::playfield {

/// Geometría del anillo de tiras. Todos los parámetros son compile-time (NTTP).
template <eng::u16 ViewportW = 320u, eng::u16 ViewportH = 208u, eng::u8 Planes = 5u,
	  eng::u16 TileW = 16u, eng::u16 TileH = 16u, eng::u16 GuardWords = 2u,
	  eng::u16 FetchExtraWords = 1u, bool SplitVertical = false, eng::u16 RingWords = 0u,
	  eng::u16 RingLines = 0u, eng::u16 MapWords = 0u>
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
	/// **Ancho del anillo** en words. Tres formas, por orden de prioridad:
	/// - `MapWords != 0`: **mapa toroidal** de periodo `MapWords` (words). El anillo se deriva como
	///   `visible + MapWords` (k=1): contiene el mapa completo + una pantalla de solape, el slot `s`
	///   vale siempre la columna `s % MapWords` y **no hay que repintar la ventana al envolver**.
	///   Es la forma recomendada; impide por construcción el fallo de "span" no múltiplo del mapa.
	/// - `RingWords != 0`: ancho de anillo explícito (p. ej. bitmap de mapa largo, o fondo que
	///   repite con periodo divisor del span). El llamador garantiza la coherencia.
	/// - ambos 0: **pantalla + guarda + fetch** (fondo que se repite con ese periodo).
	///
	/// **Invariante**: el puntero recorre `ring_w_words - visible_words` posiciones antes de
	/// envolver; esa `span` debe ser múltiplo del periodo del mundo o el contenido se descuadra al
	/// envolver. Ver HOST-244 (invariante de contenido).
	static constexpr eng::u16 ring_w_words =
		(MapWords != 0u)
			? static_cast<eng::u16>(visible_words + MapWords)
			: ((RingWords != 0u) ? RingWords
					     : static_cast<eng::u16>(visible_words + GuardWords + FetchExtraWords));
	static_assert(ring_w_words >= visible_words + GuardWords + FetchExtraWords,
		      "el anillo debe caber al menos pantalla + guarda + fetch");
	/// Con `MapWords`, el periodo del mapa es exactamente la `span` del anillo (multiplo trivial).
	static_assert(MapWords == 0u || (ring_w_words - visible_words) % MapWords == 0u,
		      "el periodo del mapa debe dividir la span del anillo (o se descuadra al envolver)");
	static constexpr eng::u16 ring_w_bytes = static_cast<eng::u16>(ring_w_words * 2u);
	/// Ancho **fetcheado** por el display por linea (viewport + 1 palabra de scroll).
	static constexpr eng::u16 fetch_words = static_cast<eng::u16>(visible_words + FetchExtraWords);
	/// Salto de fila del display interleaved: apunta a la MISMA posicion de la linea siguiente
	/// (`planes*ring_w_bytes`) menos lo ya avanzado por el fetch (`fetch_words*2`).
	static constexpr eng::u16 bpl_mod =
		static_cast<eng::u16>(static_cast<eng::u32>(Planes) * ring_w_bytes - fetch_words * 2u);
	/// Anillo vertical: alto del bitmap (2 pantallas para el scroll Y, o viewport + guarda si split).
	static constexpr eng::u16 ring_h =
		(RingLines != 0u) ? RingLines
				  : static_cast<eng::u16>(ViewportH + (SplitVertical ? 2u * TileH : 0u));
	/// Tiles de una columna completa (alto del anillo) y de una fila completa (ancho del anillo).
	static constexpr eng::u16 column_tiles = static_cast<eng::u16>(ring_h / TileH);
	static constexpr eng::u16 row_tiles = ring_w_words;
	/// Planelíneas de una columna completa (alto del anillo x planos).
	static constexpr eng::u16 column_planelines = static_cast<eng::u16>(ring_h * Planes);
	/// `BLTSIZE` tiene H de 10 bits (max 1024 planelíneas) -> la columna se parte en varios blits.
	static constexpr eng::u16 max_blt_h = 1024u;
	static constexpr eng::u8 column_blits =
		static_cast<eng::u8>((column_planelines + max_blt_h - 1u) / max_blt_h);
	static_assert(column_blits >= 1u, "columna necesita al menos 1 blit");
	/// Tira HORIZONTAL (fila) para el scroll Y: ancho = anillo, alto = tile_h x planos. El blit
	/// reutiliza el de columna con estos parametros (`{strip_row_words, row_planelines, bltdmod_row}`).
	static constexpr eng::u16 strip_row_words = ring_w_words;
	static constexpr eng::u16 row_planelines = static_cast<eng::u16>(TileH * Planes);
	static constexpr eng::u16 bltdmod_row =
		static_cast<eng::u16>(ring_w_bytes - ring_w_words * 2u);
	/// Ancho de la tira de COLUMNA en palabras (tile 16 -> 1, tile 32 -> 2) y su BLTDMOD.
	static constexpr eng::u16 strip_words = static_cast<eng::u16>(TileW / 16u);
	static constexpr eng::u16 bltdmod_col = static_cast<eng::u16>(ring_w_bytes - strip_words * 2u);
};

/// Decisión de un frame del scroller de tiras (lo que el backend debe ejecutar/parchear).
struct StripFrame {
	bool column_crossed = false;   ///< cruce de frontera de tile en X -> pintar la columna entrante
	eng::u16 col_dest_word = 0;     ///< palabra del anillo (destino de la tira), FUERA de la ventana
	bool row_crossed = false;       ///< cruce en Y -> pintar la fila entrante (solo si SplitVertical)
	eng::u16 row_dest_line = 0;     ///< linea del anillo vertical
	eng::u16 window_word = 0;       ///< base de la ventana visible (BPLxPT), palabra del anillo
	eng::u16 window_line = 0;       ///< base vertical de la ventana en el anillo (Y, en lineas)
	eng::u8 bplcon1_fine = 0;       ///< fine scroll (BPLCON1), 0..15
	eng::u8 blits = 0;              ///< tiras a pintar este frame (column + row, <= 2)
};

/// **Planificador**: dado el scroll actual `(sx, sy)` y el previo `(psx, psy)` (en px, no negativos),
/// decide las tiras a pintar y los valores de Copper. El orden real es: pintar la tira (destination
/// en la guarda, invisible) -> parchear Copper -> el haz muestra la ventana nueva.
template <class Geom>
[[nodiscard]] constexpr StripFrame plan_strip_frame(const Geom& g, eng::s32 sx, eng::s32 sy,
						    eng::s32 psx, eng::s32 psy) noexcept {
	StripFrame f {};
	// Convencion canonica (`playfield_scroll.hpp`): el puntero apunta al COARSE `(x-1) & ~15` y
	// BPLCON1 lleva el retardo `(16 - (x&15)) & 15`. El puntero de la ventana NO puede envolver el
	// anillo (el display lee la ventana CONTIGUA): recorre `[0, ring_w_words - visible_words]`.
	const eng::u16 sxu = (sx < 1) ? 1u : static_cast<eng::u16>(sx);
	const eng::u16 pxu = (psx < 1) ? 1u : static_cast<eng::u16>(psx);
	const eng::u16 coarse = eng::graphics::fine_scroll_coarse(sxu);
	const eng::u16 pcoarse = eng::graphics::fine_scroll_coarse(pxu);
	const eng::u16 span = static_cast<eng::u16>(g.ring_w_words - g.visible_words);
	f.window_word = static_cast<eng::u16>((coarse / 16u) % span);
	f.bplcon1_fine = static_cast<eng::u8>(eng::graphics::fine_delay(sxu));
	// Ventana VERTICAL: si el anillo es todo el alto del mapa (`ring_h > viewport_h`), el scroll Y
	// es solo el offset de fila (sin split: el bitmap ya contiene todas las filas, la tira X las
	// rellena enteras). La camara Y queda acotada a `[0, ring_h - viewport_h]`.
	{
		const eng::s32 max_line = static_cast<eng::s32>(g.ring_h) -
					  static_cast<eng::s32>(g.viewport_h);
		eng::s32 wl = sy;
		if (wl < 0) wl = 0;
		if (wl > max_line) wl = max_line;
		f.window_line = static_cast<eng::u16>(wl);
	}
	// La columna entrante se revela cuando cambia el COARSE (frontera de tile).
	if (coarse != pcoarse) {
		f.column_crossed = true;
		const eng::u16 pw = static_cast<eng::u16>((pcoarse / 16u) % span);
		f.col_dest_word = (sx > psx)
			? static_cast<eng::u16>((pw + g.visible_words) % g.ring_w_words)
			: static_cast<eng::u16>((pw + g.ring_w_words - 1u) % g.ring_w_words);
		++f.blits;
	}
	// `g.split_vertical` es `static constexpr` en el NTTP (el `if` se pliega a nada) y un miembro
	// en la geometría runtime (se evalúa por frame).
	if (g.split_vertical) {
		if ((sy / g.tile_h) != (psy / g.tile_h)) {
			f.row_crossed = true;
			f.row_dest_line = static_cast<eng::u16>((static_cast<eng::u32>(psy) / g.tile_h) %
								(g.ring_h / g.tile_h));
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
[[nodiscard]] constexpr StripCopper strip_copper_values(const Geom& g,
							const StripFrame& f) noexcept {
	StripCopper c {};
	// `BPLCON1` lleva el retardo fino **duplicado en los DOS nibbles** (nibble bajo = PF1, alto =
	// PF2). En un playfield single el nibble alto sigue afectando a los planos pares (BPL2/4/6): con
	// solo el nibble bajo, **el plano central no recibe el fino y se desplaza en saltos de 16 px**
	// (mismo convenio que `amiga_display_mapper.hpp`/`tile_scroll.hpp` y la demo 120).
	const eng::u16 nibble = f.bplcon1_fine;
	c.bplcon1 = static_cast<eng::u16>(nibble | static_cast<eng::u16>(nibble << 4u));
	c.planes = g.planes;
	// Offset vertical (Y) de la ventana: `window_line` planelíneas completas (planos*anillo).
	const eng::u32 y_off = static_cast<eng::u32>(f.window_line) * g.planes * g.ring_w_bytes;
	for (eng::u8 p = 0u; p < g.planes; ++p) {
		c.pt_byte[p] = y_off + static_cast<eng::u32>(p) * g.ring_w_bytes +
			       static_cast<eng::u32>(f.window_word) * 2u;
	}
	if (g.split_vertical) {
		c.split_line = g.split_line;
		c.split_two_wait = g.split_crosses_255;
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

/// Descriptor del blit de la tira para el `ash` (fine shift 0..15) y el `chunk` (trozo de
/// `<= 1024` planelíneas) dados.
template <class Geom>
[[nodiscard]] constexpr StripBlit strip_blit_desc(const Geom& g, eng::u8 ash,
						  eng::u8 chunk = 0u) noexcept {
	StripBlit b {};
	b.bltcon0 = static_cast<eng::u16>(eng::graphics::kBlitterUseA | eng::graphics::kBlitterUseD |
					  eng::graphics::kBlitterMintermCopyA |
					  (static_cast<eng::u16>(ash) << 12u));
	b.bltcon1 = 0u;
	b.bltafwm = 0xffffu;
	b.bltalwm = 0xffffu;
	b.bltamod = 0;
	b.bltdmod = g.bltdmod_col;
	const eng::u16 start = static_cast<eng::u16>(chunk) * g.max_blt_h;
	const eng::u16 remaining = static_cast<eng::u16>(g.column_planelines - start);
	const eng::u16 h = remaining < g.max_blt_h ? remaining : g.max_blt_h;
	b.bltsize = static_cast<eng::u16>((h << 6u) | g.strip_words);
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
[[nodiscard]] constexpr eng::u16 compose_column(const Geom& g, eng::u16* dst,
						const eng::u16* tile_bank, const eng::u16* tile_ids,
						eng::u16 bank_stride_words) noexcept {
	const eng::u16 tile_words = static_cast<eng::u16>(g.tile_h * g.planes);
	eng::u16 w = 0u;
	for (eng::u16 t = 0u; t < g.column_tiles; ++t) {
		const eng::u16* src = tile_bank + static_cast<eng::u32>(tile_ids[t]) * bank_stride_words;
		for (eng::u16 i = 0u; i < tile_words; ++i) dst[w++] = src[i];
	}
	return w;
}

/// **Compone una fila completa** (viewport entero) concatenando los `Geom::row_tiles` tiles del banco
/// (cada uno `tile_h*planes` palabras) en `dst` **contiguo**, interleaved (por linea, por plano, por
/// columna), listo para el blit de fila con `BLTAMOD=0`. Tiles **separados**, copiados uno a uno.
/// Devuelve las palabras escritas (`row_tiles*tile_h*planes`).
template <class Geom>
[[nodiscard]] constexpr eng::u16 compose_row(const Geom& g, eng::u16* dst,
					     const eng::u16* tile_bank, const eng::u16* tile_ids,
					     eng::u16 bank_stride_words) noexcept {
	const eng::u16 cols = g.row_tiles;
	eng::u16 w = 0u;
	for (eng::u16 line = 0u; line < g.tile_h; ++line) {
		for (eng::u8 plane = 0u; plane < g.planes; ++plane) {
			for (eng::u16 c = 0u; c < cols; ++c) {
				const eng::u16* src = tile_bank +
					static_cast<eng::u32>(tile_ids[c]) * bank_stride_words;
				dst[w++] = src[static_cast<eng::u32>(line) * g.planes + plane];
			}
		}
	}
	return w;
}

/// **Controlador del scroller de tiras**: reúne el trabajo de **CPU + Blitter** por frame
/// (planificar la tira → componer la columna entrante → blitearla en el anillo) sobre un `Geom`,
/// un `Map` (ids de tile) y un `Sink` (el backend, con `blitter_strip_column`). El lado de Copper
/// lo aporta `StripComposer` (`strip_copper_values` + `patch`): juntos son el driver completo.
///
/// El llamador (demo/juego/fachada) solo pasa el scroll por frame: `fill_ring()` en el setup y
/// `tick(scroll, prev)` por frame. Devuelve el `StripFrame` para encadenar el patch de Copper. El
/// **anillo debe dimensionarse con `MapWords`** (periodo del mapa) para que `period` sea la `span`
/// del puntero y el contenido no se descuadre al envolver (ver `StripScrollGeometry` y HOST-244).
///
/// Contratos: `Map::tile_at(u16 col, u16 row) -> u16` (id de tile del banco) y
/// `Sink::blitter_strip_column(src, dst, words, dmod, planelines, shift) -> bool`.
template <class Geom, class Map, class Sink>
class StripScrollController {
public:
	/// Máximo de tiles de una columna (alto del anillo / alto de tile). Cota del buffer de ids
	/// (`column_tiles` puede ser runtime con `RuntimeScrollGeometry`, así que no vale un VLA).
	static constexpr eng::u8 kMaxColumnTiles = 64u;

	/// **Fija la geometría** (instancia). El NTTP `StripScrollGeometry` la aporta por su tipo
	/// (todos sus miembros son `static constexpr`, accesibles igual vía una instancia) y
	/// `RuntimeScrollGeometry` la trae de runtime. Llámalo en el setup, antes de `bind`/`fill_ring`.
	constexpr void set_geometry(const Geom& g) noexcept { m_geom = g; }
	[[nodiscard]] constexpr const Geom& geometry() const noexcept { return m_geom; }

	/// Periodo del mapa en words (== `span` del puntero): `ring - visible`.
	[[nodiscard]] constexpr eng::u16 period() const noexcept {
		return static_cast<eng::u16>(m_geom.ring_w_words - m_geom.visible_words);
	}

	/// Liga los buffers (anillo/banco/columna, en Chip) y los observadores del mapa y del backend
	/// (`Ref`, no propietarios). Se llama una vez en el setup, antes de `fill_ring()`.
	constexpr void bind(eng::u16* ring, const eng::u16* bank, eng::u16* column,
			    eng::u16 bank_stride_words, Map& map, Sink& sink) noexcept {
		m_ring = ring;
		m_bank = bank;
		m_column = column;
		m_bank_stride = bank_stride_words;
		m_map = map;
		m_sink = sink;
	}

	/// **Setup**: pre-pinta TODA la ventana del anillo con base 0 (el slot `s` vale la columna
	/// `s % period`). Cubre también los slots de solape para que ningún píxel quede sin inicializar.
	void fill_ring() noexcept {
		for (eng::u16 w = 0u; w < m_geom.ring_w_words; ++w) {
			paint_column(w, static_cast<eng::u16>(w % period()));
		}
	}

	/// **Frame**: planifica la tira y, si cruza frontera de tile, pinta la columna entrante en la
	/// guarda. `(x, y)` es la cámara en px (Y solo mueve la ventana vertical: el bitmap ya tiene
	/// todas las filas). Devuelve el `StripFrame` (para `strip_copper_values`/`patch`).
	[[nodiscard]] StripFrame tick(eng::s32 x, eng::s32 y, eng::s32 prev_x,
				      eng::s32 prev_y) noexcept {
		const StripFrame fr = plan_strip_frame(m_geom, x, y, prev_x, prev_y);
		if (fr.column_crossed) {
			const eng::u16 sxu = (x < 1) ? 1u : static_cast<eng::u16>(x);
			const eng::u32 coarse_w = eng::graphics::fine_scroll_coarse(sxu) / 16u;
			const eng::u32 span = m_geom.ring_w_words - m_geom.visible_words;
			const eng::u32 offset = coarse_w - (coarse_w % span);
			paint_column(fr.col_dest_word,
				     static_cast<eng::u16>((fr.col_dest_word + offset) % period()));
		}
		return fr;
	}

private:
	/// Compone la columna `map_col` (tiles del `Map`) y la blitea en el word `ring_word` del anillo.
	void paint_column(eng::u16 ring_word, eng::u16 map_col) noexcept {
		eng::u16 ids[kMaxColumnTiles] {};
		const eng::u16 tiles = m_geom.column_tiles < kMaxColumnTiles ? m_geom.column_tiles
									     : kMaxColumnTiles;
		for (eng::u16 r = 0u; r < tiles; ++r) {
			ids[r] = static_cast<eng::u16>(m_map->tile_at(map_col, r));
		}
		(void)compose_column(m_geom, m_column, m_bank, ids, m_bank_stride);
		(void)m_sink->blitter_strip_column(m_column, m_ring + ring_word, m_geom.strip_words,
						   m_geom.bltdmod_col, m_geom.column_planelines, 0u);
	}

	Geom m_geom {};
	eng::u16* m_ring = nullptr;
	const eng::u16* m_bank = nullptr;
	eng::u16* m_column = nullptr;
	eng::u16 m_bank_stride = 0u;
	eng::Ref<Map> m_map {};
	eng::Ref<Sink> m_sink {};
};

} // namespace eng::playfield
