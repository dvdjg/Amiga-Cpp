#pragma once

/// \file corner_tiles.hpp
/// **Autotiling por esquinas (Wang 2-corner / dual-grid / marching squares)**
/// (`eng::gfx::proc`): un tile se elige por la **máscara de 4 esquinas** del
/// occupancy (2 terrenos → 16 variantes). Es la pieza para lagos, orillas, ríos
/// anchos, islas y penínsulas **cerradas por construcción**: el motivo vive en
/// las esquinas, no en el centro del tile, y dos vecinos comparten el valor de
/// la esquina común, así que las juntas casan siempre.
///
/// ```text
///   occupancy (celdas de datos, una malla (W+1)×(H+1))    render dual-grid
///        W W W                                              cada tile cubre 4 esquinas
///        W G G   ───corner_mask(occ, tx, ty)──▶ 0..15 ───▶  y usa la variante `m`
///        G G G
///
///   máscara (eng::u8):  bit0 NW (occ tx,   ty  )   bit1 NE (occ tx+1, ty  )
///                          bit2 SW (occ tx,   ty+1)   bit3 SE (occ tx+1, ty+1)
///                          1 = terreno B (p. ej. agua), 0 = terreno A (hierba)
/// ```
///
/// ## Los dos pintores
///
/// - **`paint_corner_organic`**: campo **bilineal Q8** de las esquinas (transición
///   suave A↔B) desplazado por **ruido entero tileable** (`noise_tile.hpp`,
///   `mulu.w`; sin escalar, sin libcalls — la vía apta para el 68000 en carga) y
///   franja de orilla con **dither Bayer** hacia el lado dominante. La orilla es
///   irregular pero **continua en la junta**: el campo depende solo de las
///   esquinas compartidas y el período del ruido en píxeles es exactamente el
///   lado del tile.
/// - **`paint_corner_hard`**: marching squares puro, bordes nítidos (pixel-art).
///   La ambigüedad de las sillas (máscaras 6/9, esquinas activas en diagonal) se
///   resuelve por **cuadrantes**: cada esquina activa pinta su cuarto, nunca una
///   banda diagonal que cruce la junta de forma ambigua.
///
/// El ruido del set es **único por tileset** (no una semilla por máscara):
/// máscaras contiguas son distintas y, con semillas distintas, la ondulación no
/// casaría en la junta. La variedad la dan la propia máscara y el material; la
/// repetición de la ondulación se limita con `CornerNoise::freq` (ciclos de ruido
/// por tile). Es una corrección deliberada del primer boceto (semilla
/// `s + m·17` por máscara), que rompía la continuidad de la orilla.
///
/// ## Materiales
///
/// `paint_corner_materials` pinta el mismo campo muestreando el `TextureAtlas`
/// (A, B y orilla) con **variante fija** (las texturas del atlas son tileables con
/// período `TW`: la variante 0 continúa el patrón en el tile vecino, igual que el
/// cauce de `terrain_paint.hpp`).
///
/// ## Coste
///
/// Todo es **generación** (carga), nunca por frame: productos 16×16 (`mulu.w`),
/// shifts y lookups; sin divisiones en el bucle (el período del ruido y `u/v`
/// son `constexpr`). Presupuesto: `docs/guides/optimization/METODO_OPTIMIZACION.md`.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-448`); sin demo que la ejercite en hardware.
/// Diseño y uso: `docs/engine/architecture/PROCEDURAL_BITMAPS.md` §Transiciones por esquinas.

#include <eng/core/math/fast_div.hpp>
#include <eng/core/math/random.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>
#include <eng/graphics/proc/atlas.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/dither.hpp>
#include <eng/graphics/proc/noise_tile.hpp>

namespace eng::gfx::proc {

/// Esquina de la celda de render. La **máscara de las 4 esquinas** viaja como
/// `eng::u8` (0..15): bit0 NW, bit1 NE, bit2 SW, bit3 SE; `1` = terreno B (el que
/// "entra"), `0` = terreno A (el fondo). Es un valor de dominio, no un alias de
/// escalar (el gate `generic-headers` veta fijar tipos crudos en cabeceras, AGENTS §1.11).
enum class Corner : eng::u8 { NorthWest = 0, NorthEast, SouthWest, SouthEast };

/// ¿La máscara `m` tiene activa la esquina `c`?
/// \param m máscara 0..15. \param c esquina. \return true si el bit está a 1.
[[nodiscard]] constexpr bool corner_set(eng::u8 m, Corner c) {
	return (m & (1u << eng::util::to_underlying(c))) != 0u;
}

/// **Mapa de ocupación a resolución de celda** (no de píxel): `1` = terreno B.
/// Malla del dual-grid: para `TW×TH` tiles de render se usa `(TW+1)×(TH+1)`.
/// \tparam CW,CH celdas de la malla (≤ 256: los stamps enteros usan `u16`/`u32`).
template <eng::u16 CW, eng::u16 CH>
struct OccupancyMap {
	static_assert(CW > 0u && CH > 0u && CW <= 256u && CH <= 256u,
		      "el occupancy del autotiling por esquinas es 1..256 por eje");
	/// Celdas fila a fila (`cells[y*CW+x]`), valor 0/1 hoy (el tipo admite otros usos).
	eng::u8 cells[eng::u32(CW) * CH] {};
	/// Valor que devuelve `at()` fuera del mapa (0 = el terreno A rodea el mapa).
	eng::u8 outside = 0u;
	/// Ancho de la malla en celdas.
	static constexpr eng::u16 width = CW;
	/// Alto de la malla en celdas.
	static constexpr eng::u16 height = CH;

	/// Celda `(x, y)` de solo lectura; fuera del mapa devuelve `outside`.
	/// \param x,y celda (admite negativos: el borde del mapa es un valor, no un fallo).
	/// \return valor de la celda (0/1).
	[[nodiscard]] constexpr eng::u8 at(eng::s16 x, eng::s16 y) const {
		if (x < 0 || y < 0 || x >= eng::s16(CW) || y >= eng::s16(CH)) return outside;
		return cells[eng::u32(y) * CW + eng::u32(x)];
	}
	/// Celda `(x, y)` mutable (sin comprobación de rango; la usan los stamps).
	/// El nombre `cell` (no `at`) evita que una llamada con `int` escoja este
	/// overload en vez del `at` comprobado: `at(-1, 0)` debe devolver `outside`.
	[[nodiscard]] constexpr eng::u8& cell(eng::u16 x, eng::u16 y) {
		return cells[eng::u32(y) * CW + eng::u32(x)];
	}
	/// Escribe el valor de una celda del mapa (sin comprobación de rango).
	/// \param x,y celda. \param v valor.
	constexpr void set(eng::u16 x, eng::u16 y, eng::u8 v) { cells[eng::u32(y) * CW + eng::u32(x)] = v; }
	/// Rellena toda la malla con `v`.
	/// \param v valor (0 = todo terreno A).
	constexpr void fill(eng::u8 v) {
		for (eng::u32 i = 0u; i < eng::u32(CW) * CH; ++i) cells[i] = v;
	}
};

/// **Máscara de las 4 esquinas** de la celda de render `(tx, ty)`: convención
/// dual-grid — el tile cubre `occ(tx,ty)`, `occ(tx+1,ty)`, `occ(tx,ty+1)` y
/// `occ(tx+1,ty+1)` (la esquina superior izquierda del tile es la celda).
/// \tparam CW,CH malla. \param occ mapa. \param tx,ty tile (0..CW-2, 0..CH-2).
/// \return máscara 0..15 (1 = esquina de terreno B).
template <eng::u16 CW, eng::u16 CH>
[[nodiscard]] constexpr eng::u8 corner_mask(const OccupancyMap<CW, CH>& occ, eng::u16 tx,
					       eng::u16 ty) {
	eng::u8 m = 0u;
	if (occ.at(eng::s16(tx), eng::s16(ty)) != 0u) m |= 1u;
	if (occ.at(eng::s16(tx) + 1, eng::s16(ty)) != 0u) m |= 2u;
	if (occ.at(eng::s16(tx), eng::s16(ty) + 1) != 0u) m |= 4u;
	if (occ.at(eng::s16(tx) + 1, eng::s16(ty) + 1) != 0u) m |= 8u;
	return m;
}

/// Parámetros del ruido de orilla del pintor orgánico.
struct CornerNoise {
	/// Semilla del campo de ruido (uno por tileset: ver nota del fichero).
	eng::u16 seed = 0u;
	/// Amplitud del empuje del umbral en unidades Q8 (≈ ±amp/2); 0 = orilla geométrica.
	eng::u8 amp = 88u;
	/// Ciclos de ruido por tile (potencia de dos; se recorta a lo que cabe con
	/// celdas de 4 px, como máximo `N/8`).
	eng::u8 freq = 1u;
};

/// Paleta plana de un par de terrenos (índices de la paleta compartida).
struct CornerStyle {
	/// Color del terreno A (fondo).
	eng::u8 color_a = 8u;
	/// Color del terreno B (el que entra).
	eng::u8 color_b = 4u;
	/// Color de la franja de orilla (espuma); con `amp == 0` no se usa.
	eng::u8 color_edge = 6u;
};

/// Materiales del atlas del par de terrenos (variante fija 0: tileable).
struct CornerMaterials {
	/// Material del terreno A (fondo).
	MaterialId a = MaterialId::Grass;
	/// Material del terreno B (el que entra).
	MaterialId b = MaterialId::Water;
	/// Material de la franja de orilla (espuma).
	MaterialId edge = MaterialId::Foam;
};

/// Clase del píxel que devuelve `corner_classify` (luego el pintor elige color).
inline constexpr eng::u8 kCornerA = 0u;
/// \copydoc kCornerA
inline constexpr eng::u8 kCornerB = 1u;
/// \copydoc kCornerA
inline constexpr eng::u8 kCornerEdge = 2u;

/// Semi-ancho (en unidades Q8 del campo) de la franja de orilla: `|campo−128| < band`
/// se resuelve con dither hacia el lado dominante (32 ≈ ±2 px en un tile de 16).
inline constexpr eng::u8 kCornerEdgeBand = 32u;

namespace corner_detail {

/// Nodos del período del ruido con celdas de 4 px: `(N/freq)>>2`, con `freq`
/// recortada a potencia de dos que deje ≥ 2 nodos (shifts, sin divisiones: se
/// llama por píxel y en 68000 `/` es una libcall). El período en píxeles es
/// `period<<2` y **divide** a `N`, condición exacta de la continuidad en la junta
/// (el validador de `tile_noise2` exige potencia de dos ≥ 2).
/// \param n lado del tile (potencia de dos ≥ 16). \param freq ciclos pedidos.
/// \return nodos por período (2..N/4).
[[nodiscard]] constexpr eng::u8 noise_period(eng::u16 n, eng::u8 freq) {
	eng::u32 f = (freq == 0u || !eng::is_pow2(freq)) ? 1u : freq;
	while (f > 1u && f > (eng::u32(n) >> 3u)) f >>= 1u; // deja ≥ 2 nodos (n/f ≥ 8)
	return eng::u8(eng::u32(n) >> (eng::ilog2(f) + 2u));
}

/// Casilla del `freq` efectivo (potencia de dos ≤ `N/8`, sobre {1,2,4,8}).
/// \param n lado del tile. \param freq ciclos pedidos.
/// \return 1, 2, 4 u 8 (el que use `noise_period`).
[[nodiscard]] constexpr eng::u8 freq_bucket(eng::u16 n, eng::u8 freq) {
	eng::u32 f = (freq == 0u || !eng::is_pow2(freq)) ? 1u : freq;
	while (f > 1u && f > (eng::u32(n) >> 3u)) f >>= 1u;
	return eng::u8(f);
}

/// **Tabla ROM de cuadrados** `(−128..128)²` (257 entradas u16): los stamps la
/// indexan en vez de multiplicar. Motivo medido en el cruce m68k: `dx*dx` con
/// `s32` (o incluso con `s16` en según qué contexto de inlining) puede bajar a
/// `__mulsi3` cuando el optimizador pierde el rango; la tabla es una lectura.
struct SquareTable {
	/// Cuadrado de `i` en `sq[i + 128]` (0..16384).
	eng::u16 sq[257] {};
	/// Rellena la tabla (evaluación de compilación).
	constexpr SquareTable() : sq() {
		for (eng::s32 i = -128; i <= 128; ++i) sq[i + 128] = eng::u16(i * i);
	}
};

/// Cuadrados de −128..128 (ver `SquareTable`).
inline constexpr SquareTable kSquares {};

/// Muestra el ruido de orilla con `tile_noise2` pero con **parámetros literales
/// por rama** (`cell_log2 = 2` fijo; el período es `constexpr`) en vez de pasar
/// el período calculado: así el compilador m68k pliega la interpolación a
/// `mulu.w`/`muls.w` dentro de `corner_classify` y **no** emite la copia
/// genérica de `tile_noise2` (que usa `__mulsi3`; ver
/// `ROADMAP_PROCEDURAL_WORLDS.md` §Hallazgos del compilador m68k).
/// \tparam N lado del tile. \param x,y píxel local 0..N. \param noise parámetros.
/// \return valor 0..255.
template <eng::u16 N>
[[nodiscard]] constexpr eng::u8 corner_noise2(eng::u16 x, eng::u16 y, CornerNoise noise) {
	switch (freq_bucket(N, noise.freq)) {
	case 8u: return tile_noise2(x, y, 2u, noise_period(N, 8u), noise.seed);
	case 4u: return tile_noise2(x, y, 2u, noise_period(N, 4u), noise.seed);
	case 2u: return tile_noise2(x, y, 2u, noise_period(N, 2u), noise.seed);
	default: return tile_noise2(x, y, 2u, noise_period(N, 1u), noise.seed);
	}
}

} // namespace corner_detail

/// **Campo bilineal Q8** (0..255) de las esquinas de la máscara: `255` sobre una
/// esquina de terreno B, transición lineal entre ellas. Acepta `x`/`y` hasta `N`
/// (la muestra virtual justo sobre la esquina opuesta): es la propiedad que
/// garantiza la continuidad en la junta — con esquinas compartidas,
/// `corner_field_q8(N, y, m_izq) == corner_field_q8(0, y, m_der)`.
/// \tparam N lado del tile (potencia de dos ≥ 16). \param x,y píxel local 0..N.
/// \param m máscara de esquinas. \return campo 0..255.
template <eng::u16 N>
[[nodiscard]] constexpr eng::u8 corner_field_q8(eng::u16 x, eng::u16 y, eng::u8 m) {
	static_assert(N >= 16u && eng::is_pow2(N), "tile de esquinas: N potencia de dos >= 16");
	constexpr eng::u8 kShift = eng::u8(8u - eng::ilog2(N));
	const eng::u16 u = eng::u16(x << kShift); // 0..256 (256 = esquina opuesta)
	const eng::u16 v = eng::u16(y << kShift);
	// Esquinas y pesos en u16 (≤ 255/256): productos `mulu.w`, no `__mulsi3`.
	const eng::u16 nw = corner_set(m, Corner::NorthWest) ? 255u : 0u;
	const eng::u16 ne = corner_set(m, Corner::NorthEast) ? 255u : 0u;
	const eng::u16 sw = corner_set(m, Corner::SouthWest) ? 255u : 0u;
	const eng::u16 se = corner_set(m, Corner::SouthEast) ? 255u : 0u;
	const eng::u32 top = (eng::u32(nw) * eng::u16(256u - u) + eng::u32(ne) * u) >> 8u;
	const eng::u32 bot = (eng::u32(sw) * eng::u16(256u - u) + eng::u32(se) * u) >> 8u;
	return eng::u8((top * eng::u16(256u - v) + bot * v) >> 8u);
}

/// **Clase del píxel** del pintor orgánico: campo bilineal + ruido entero tileable
/// que empuja el umbral (128), con la franja de orilla resuelta por dither Bayer
/// (sin divisiones: `(band−dist)·255 > umbral·band`). Devuelve `kCornerA`,
/// `kCornerB` o `kCornerEdge`; la continuidad entre tiles vecinos es exacta porque
/// el campo depende solo de las esquinas y el período del ruido divide a `N`.
/// \tparam N lado del tile (potencia de dos ≥ 16). \param x,y píxel local 0..N.
/// \param m máscara de esquinas. \param noise parámetros del ruido.
/// \return clase del píxel (`kCornerA`/`kCornerB`/`kCornerEdge`).
template <eng::u16 N>
[[nodiscard]] constexpr eng::u8 corner_classify(eng::u16 x, eng::u16 y, eng::u8 m,
						CornerNoise noise) {
	const eng::s32 field = corner_field_q8<N>(x, y, m);
	const eng::u8 n = corner_detail::corner_noise2<N>(x, y, noise);
	// El producto en s16 (muls.w nativo; el rango lo garantiza la aritmética).
	const eng::s32 pushed =
		field + ((eng::s16(eng::s32(n) - 128) * eng::s16(noise.amp)) >> 8);
	const eng::s32 f = pushed < 0 ? 0 : (pushed > 255 ? 255 : pushed);
	if (f < eng::s32(128 - kCornerEdgeBand)) return kCornerA;
	if (f > eng::s32(128 + kCornerEdgeBand)) return kCornerB;
	// Franja de orilla: densidad de píxeles de borde decreciente hacia el lado
	// dominante (el centro de la franja es orilla sólida; el borde, dither puro).
	const eng::s32 dist = f >= 128 ? f - 128 : 128 - f;
	const eng::u16 band = kCornerEdgeBand;
	const bool edge = (eng::u32(eng::u16(band - eng::u16(dist))) * 255u) >
			  (eng::u32(eng::u16(Bayer::threshold8(x, y))) * band);
	if (edge) return kCornerEdge;
	return f >= 128 ? kCornerB : kCornerA;
}

/// **Marching squares** sin antialias: ¿el píxel `(x, y)` es terreno B con la
/// máscara `m`? Casos triviales, mitades, esquinas y sillas resueltas por
/// cuadrantes (máscara 6 = NE+SW y 9 = NW+SE: cada esquina pinta su cuarto; la
/// elección es determinista y casa en la junta, ver el fichero).
/// \tparam N lado del tile (potencia de dos ≥ 4). \param x,y píxel local.
/// \param m máscara de esquinas. \return true si el píxel es terreno B.
template <eng::u16 N>
[[nodiscard]] constexpr bool corner_solid(eng::u16 x, eng::u16 y, eng::u8 m) {
	static_assert(N >= 4u && eng::is_pow2(N), "tile de esquinas: N potencia de dos >= 4");
	const eng::u16 mid = eng::u16(N / 2u);
	switch (m & 15u) {
	case 0u: return false;                        // todo terreno A
	case 15u: return true;                        // todo terreno B
	case 3u: return y < mid;                      // mitad superior
	case 12u: return y >= mid;                    // mitad inferior
	case 5u: return x < mid;                      // mitad izquierda
	case 10u: return x >= mid;                    // mitad derecha
	case 1u: return x < mid && y < mid;           // esquina NW
	case 2u: return x >= mid && y < mid;          // esquina NE
	case 4u: return x < mid && y >= mid;          // esquina SW
	case 8u: return x >= mid && y >= mid;         // esquina SE
	case 6u: return (x >= mid) != (y >= mid);     // silla NE+SW: cuadrantes
	case 9u: return (x < mid) == (y < mid);       // silla NW+SE: cuadrantes
	case 7u: return !(x >= mid && y >= mid);      // falta SE
	case 11u: return !(x < mid && y >= mid);      // falta SW
	case 13u: return !(x >= mid && y < mid);      // falta NE
	case 14u: return !(x < mid && y < mid);       // falta NW
	default: return false;                        // inalcanzable (m&15)
	}
}

/// Pinta la variante `m` con el estilo **orgánico** (campo + ruido + orilla con
/// dither). Es el pintor del `CornerTileset::generate_organic`.
/// \tparam N lado del tile. \param buf destino (se sobrescribe). \param m máscara.
/// \param style colores A/B/orilla. \param noise parámetros del ruido.
template <eng::u16 N>
constexpr void paint_corner_organic(BitmapBuffer<N, N>& buf, eng::u8 m, CornerStyle style,
				    CornerNoise noise) {
	for (eng::u16 y = 0u; y < N; ++y) {
		for (eng::u16 x = 0u; x < N; ++x) {
			const eng::u8 cls = corner_classify<N>(x, y, m, noise);
			buf.at(x, y) = cls == kCornerA ? style.color_a
				       : cls == kCornerB ? style.color_b
							 : style.color_edge;
		}
	}
}

/// Pinta la variante `m` con el estilo **duro** (marching squares, bordes nítidos).
/// Es el pintor del `CornerTileset::generate_hard`.
/// \tparam N lado del tile. \param buf destino. \param m máscara. \param style colores A/B.
template <eng::u16 N>
constexpr void paint_corner_hard(BitmapBuffer<N, N>& buf, eng::u8 m, CornerStyle style) {
	for (eng::u16 y = 0u; y < N; ++y) {
		for (eng::u16 x = 0u; x < N; ++x) {
			buf.at(x, y) = corner_solid<N>(x, y, m) ? style.color_b : style.color_a;
		}
	}
}

/// Pinta la variante `m` muestreando **materiales del atlas** (A/B/orilla) con la
/// misma clasificación orgánica. Usa la variante `variant` de los tres materiales
/// (por defecto 0): las texturas del atlas son tileables con período `TW`, así que
/// la variante 0 continúa el patrón en el tile vecino (igual que el cauce).
/// \tparam N lado del tile (= `TW` del atlas). \tparam S escalar del atlas.
/// \param buf destino. \param m máscara. \param atlas atlas de materiales.
/// \param mats materiales A/B/orilla. \param noise parámetros del ruido.
/// \param variant variante del atlas de los tres materiales (0 = la continua).
template <eng::u16 N, class S>
constexpr void paint_corner_materials(BitmapBuffer<N, N>& buf, eng::u8 m,
				      const TextureAtlas<N, N, S>& atlas, CornerMaterials mats,
				      CornerNoise noise, eng::u8 variant = 0u) {
	const BitmapBuffer<N, N>& tex_a = atlas.get(mats.a, variant);
	const BitmapBuffer<N, N>& tex_b = atlas.get(mats.b, variant);
	const BitmapBuffer<N, N>& tex_e = atlas.get(mats.edge, variant);
	for (eng::u16 y = 0u; y < N; ++y) {
		for (eng::u16 x = 0u; x < N; ++x) {
			const eng::u8 cls = corner_classify<N>(x, y, m, noise);
			buf.at(x, y) = cls == kCornerA ? tex_a.at(x, y)
				       : cls == kCornerB ? tex_b.at(x, y)
							 : tex_e.at(x, y);
		}
	}
}

/// **Atlas de las 16 transiciones** de un par de terrenos (dual-grid). Los tiles
/// se generan una vez (carga) con un ruido único por set y se blitean por máscara;
/// el occupancy de cada celda decide la máscara con `corner_mask`.
/// \tparam N lado del tile (potencia de dos ≥ 16).
template <eng::u16 N>
struct CornerTileset {
	/// Las 16 variantes, indexadas por `eng::u8`.
	BitmapBuffer<N, N> tiles[16] {};
	/// Estilo con el que se generó (colores planos; documenta el set).
	CornerStyle style {};
	/// Ruido con el que se generó (semilla del set; documenta el set).
	CornerNoise noise {};

	/// Genera las 16 variantes con el pintor orgánico (campo + ruido + orilla).
	/// \param s colores A/B/orilla. \param n parámetros del ruido (semilla del set).
	constexpr void generate_organic(CornerStyle s, CornerNoise n) {
		style = s;
		noise = n;
		for (eng::u8 m = 0u; m < 16u; ++m) paint_corner_organic<N>(tiles[m], m, s, n);
	}
	/// Genera las 16 variantes con el pintor duro (marching squares).
	/// \param s colores A/B (la orilla no se usa).
	constexpr void generate_hard(CornerStyle s) {
		style = s;
		for (eng::u8 m = 0u; m < 16u; ++m) paint_corner_hard<N>(tiles[m], m, s);
	}
	/// Variante de la máscara `m` (enmascara a 4 bits: cualquier `u8` vale).
	/// \param m máscara de esquinas. \return tile pintado de esa máscara.
	[[nodiscard]] constexpr const BitmapBuffer<N, N>& get(eng::u8 m) const {
		return tiles[m & 15u];
	}

	/// Blitea una rejilla de máscaras (`masks[ty][tx]`, salida de
	/// `fill_corner_layout`) como mapa de píxeles (composición opaca, tile a tile).
	/// \tparam TH,TW rejilla de máscaras. \tparam OutW,OutH destino (TW·N × TH·N).
	/// \param masks máscaras por celda. \param dest destino (se sobrescribe).
	template <eng::u16 TH, eng::u16 TW, eng::u16 OutW, eng::u16 OutH>
	constexpr void blit(const eng::u8 (&masks)[TH][TW], BitmapBuffer<OutW, OutH>& dest) const {
		static_assert(OutW == TW * N && OutH == TH * N,
			      "el destino de blit_corner debe ser (TW·N)×(TH·N)");
		for (eng::u16 ty = 0u; ty < TH; ++ty) {
			for (eng::u16 tx = 0u; tx < TW; ++tx) {
				const BitmapBuffer<N, N>& t = get(masks[ty][tx]);
				for (eng::u16 y = 0u; y < N; ++y) {
					for (eng::u16 x = 0u; x < N; ++x) {
						dest.at(eng::u16(tx * N + x), eng::u16(ty * N + y)) =
							t.at(x, y);
					}
				}
			}
		}
	}

	/// Compone el mapa completo desde el occupancy: calcula las máscaras
	/// (`fill_corner_layout`) y blitea el set. Atajo de una llamada para el caso
	/// canónico (occupancy de `(TW+1)×(TH+1)`).
	/// \tparam CW,CH malla del occupancy. \tparam OutW,OutH destino ((CW−1)·N × (CH−1)·N).
	/// \param occ ocupación (1 = terreno B). \param dest destino (se sobrescribe).
	template <eng::u16 CW, eng::u16 CH, eng::u16 OutW, eng::u16 OutH>
	constexpr void compose(const OccupancyMap<CW, CH>& occ, BitmapBuffer<OutW, OutH>& dest) const {
		eng::u8 masks[CH - 1u][CW - 1u] {};
		fill_corner_layout(occ, masks);
		blit(masks, dest);
	}
};

/// Rellena `masks[ty][tx]` con la máscara de esquinas de cada celda de render.
/// El occupancy debe ser la malla dual-grid completa: `(TW+1)×(TH+1)` celdas.
/// \tparam CW,CH malla del occupancy. \tparam TH,TW tiles de render (deducidos del array).
/// \param occ ocupación. \param masks máscaras de salida (row-major).
template <eng::u16 CW, eng::u16 CH, eng::u16 TH, eng::u16 TW>
constexpr void fill_corner_layout(const OccupancyMap<CW, CH>& occ, eng::u8 (&masks)[TH][TW]) {
	static_assert(CW == TW + 1u && CH == TH + 1u,
		      "dual-grid: el occupancy es (tiles+1) en cada eje");
	for (eng::u16 ty = 0u; ty < TH; ++ty) {
		for (eng::u16 tx = 0u; tx < TW; ++tx) {
			masks[ty][tx] = corner_mask(occ, tx, ty);
		}
	}
}

/// Sella un **rectángulo** de `v` en el occupancy (caso base de los stamps).
/// \tparam CW,CH malla. \param occ mapa. \param x0,y0 esquina. \param w,h tamaño.
/// \param v valor (1 = terreno B).
template <eng::u16 CW, eng::u16 CH>
constexpr void stamp_rect(OccupancyMap<CW, CH>& occ, eng::u16 x0, eng::u16 y0, eng::u16 w,
			  eng::u16 h, eng::u8 v = 1u) {
	for (eng::u16 y = y0; y < eng::u16(y0 + h) && y < CH; ++y) {
		for (eng::u16 x = x0; x < eng::u16(x0 + w) && x < CW; ++x) occ.set(x, y, v);
	}
}

/// Sella un **disco elíptico** con contorno ruidoso (a resolución de celda): la
/// base de lagos, islas (disco con `v`=1 y disco interior con `v`=0) y charcas.
/// Sin `%`/`/` ni `sqrt`: comparación entera `dx²·ry² + dy²·rx² ≤ rx²·ry²` con un
/// **anillo de borde** (`rr>>2`, ~medio celda de grosor) donde una rejilla de
/// `tile_noise2` (a 8 muestras por celda, determinista) decide dilatar/erodir
/// celda a celda → contorno irregular **y contiguo** (sin motas sueltas: los
/// sellos del anillo pegan con el núcleo). Solo escribe dentro del anillo y el
/// núcleo: fuera, el occupancy queda como estaba.
/// Requisitos: malla ≤ 256 y radios ≤ 128 (las cuentas caben en `u32`).
/// \tparam CW,CH malla. \param occ mapa. \param cx,cy centro (en celdas).
/// \param rx,ry radios (en celdas). \param v valor a sellar (1 = terreno B).
/// \param seed semilla del contorno.
template <eng::u16 CW, eng::u16 CH>
constexpr void stamp_disc(OccupancyMap<CW, CH>& occ, eng::s16 cx, eng::s16 cy, eng::u16 rx,
			  eng::u16 ry, eng::u8 v, eng::u32 seed) {
	if (rx == 0u || ry == 0u || rx > 128u || ry > 128u) return;
	// Radios y productos en u16 (≤ 16384 y ≤ 65025): las multiplicaciones bajan a
	// `mulu.w` (nativas) y no a `__mulsi3`, también en la ruta runtime del 68000.
	const eng::u16 rx2 = eng::u16(eng::u32(rx) * rx);
	const eng::u16 ry2 = eng::u16(eng::u32(ry) * ry);
	const eng::u32 rr = eng::u32(rx2) * ry2;
	const eng::u32 band = rr >> 2u; // anillo de borde (≈ medio celda por eje)
	for (eng::u16 y = 0u; y < CH; ++y) {
		for (eng::u16 x = 0u; x < CW; ++x) {
			// dx/dy en s16 y sus cuadrados por tabla ROM: el producto s16·s16
			// puede degradar a `__mulsi3` en contexto de inlining (ver kSquares).
			const eng::s16 dx = eng::s16(eng::s32(x) - cx);
			const eng::s16 dy = eng::s16(eng::s32(y) - cy);
			if (dx < -128 || dx > 128 || dy < -128 || dy > 128) continue; // fuera de la caja
			const eng::u16 dx2 = corner_detail::kSquares.sq[eng::u16(dx + 128)];
			const eng::u16 dy2 = corner_detail::kSquares.sq[eng::u16(dy + 128)];
			if (dx2 > rx2 || dy2 > ry2) continue; // fuera de la elipse (caja)
			const eng::u32 lhs = eng::u32(dx2) * ry2 + eng::u32(dy2) * rx2;
			if (lhs + band < rr) {
				occ.set(x, y, v); // núcleo: dentro garantizado
				continue;
			}
			if (lhs > rr + band) continue; // fuera del anillo: no toca
			// Anillo: el ruido dilata (mitad exterior) o erode (mitad interior).
			const eng::u8 n =
				tile_noise2(eng::u16(x * 8u), eng::u16(y * 8u), 3u, 8u, eng::u16(seed));
			if (n >= 128u) occ.set(x, y, v);
		}
	}
}

/// Traza un **río** recto-meandriforme de `width` celdas a lo largo del mapa
/// (N→S por defecto; `vertical = false` para O→E). El meandro (±1 celda, 1 de
/// cada 3 filas) no cambia de anchura: 1..3 celdas por fila, sin huecos.
/// \tparam CW,CH malla. \param occ mapa. \param start celda de arranque (en el eje corto).
/// \param width anchura del cauce en celdas. \param seed semilla de los meandros.
/// \param vertical true = N→S, false = O→E (transpuesto).
template <eng::u16 CW, eng::u16 CH>
constexpr void stamp_river(OccupancyMap<CW, CH>& occ, eng::u16 start, eng::u16 width,
			   eng::u32 seed, bool vertical = true) {
	if (width == 0u) return;
	const eng::u16 len = vertical ? CH : CW;
	const eng::u16 cross = vertical ? CW : CH;
	if (width > cross) return;
	eng::Xoroshiro64pp rng {seed, seed ^ 0x521135u};
	// Rango del borde oeste de la banda: [1, cross-width-1] si cabe, si no pegado a 0.
	const bool room = cross > width + 2u;
	const eng::s16 max_x = room ? eng::s16(cross - width - 1u) : 0;
	eng::s16 x = (start < cross) ? eng::s16(start) : 0; // arranque fuera del eje: a 0
	if (room) {
		if (x < 1) x = 1;
		if (x > max_x) x = max_x;
	} else {
		x = 0;
	}
	for (eng::u16 i = 0u; i < len; ++i) {
		for (eng::u16 d = 0u; d < width; ++d) {
			const eng::u16 c = eng::u16(x + eng::s16(d));
			if (vertical) {
				occ.set(c, i, 1u);
			} else {
				occ.set(i, c, 1u);
			}
		}
		if (room && eng::chance(rng, 1u, 3u)) {
			x = eng::s16(x + eng::s16(rng.next_mod(3u)) - 1);
			if (x < 1) x = 1;
			if (x > max_x) x = max_x;
		}
	}
}

} // namespace eng::gfx::proc
