#pragma once

/// \file flat_shade_xor.hpp
/// **Técnica de relleno por contorno + area fill XOR** (`eng::retro`, Amiga): dibuja las
/// aristas visibles de un sólido convexo como líneas `EOR` (`ONEDOT`) y luego **un solo**
/// area fill `XOR`; el interior queda relleno por **paridad de scanline**.
///
/// No es el relleno por cara (`Surface::fill_polygon`): es la técnica del original de
/// `flatshade-convex` (demo 116), específica del Blitter Amiga:
///
/// 1. `blitter_lines_begin` fija los comunes del modo línea una vez.
/// 2. Por arista visible y por plano con su bit de color: `blitter_line_draw` (EOR,
///    `ONEDOT`), con `BLTDPTR` en la **base del bitmap** (paridad par/impar correcta en
///    los vértices). Las aristas **horizontales** se descartan (no aportan cruce útil y
///    meterían píxeles de contorno en los vértices).
/// 3. `blitter_area_fill` (XOR, descendente) conmuta el relleno del interior.
///
/// Requiere un sólido **convexo**: tras el back-face culling, las caras visibles
/// particionan la silueta, así que el contorno es cerrado y la paridad rellena solo el
/// interior. La técnica es **frágil** (un píxel de contorno mal puesto rompe la paridad y
/// filtra una banda): su gate válido es visual (`freeze-diff`), no la cobertura.
///
/// El `color` de cada arista es la **XOR de las luces** de las caras adyacentes (las
/// aristas internas se cancelan y quedan la silueta y las aristas de contraste), como en
/// `lib3d::update_edge_visibility_convex`.
///
/// `Backend` es la capa de plataforma (`AmigaBackend` en Amiga) que aporta las
/// primitivas de Blitter; esta cabecera no conoce registros.
///
/// ## CONTRATO DE PARIDAD (robustez del polígono — LEER antes de tocar nada)
///
/// El relleno XOR es correcto **si y solo si** se mantienen TODOS estos invariantes;
/// cada uno tiene un fallo real medido detrás y su rompimiento no lo detecta la
/// cobertura (solo una secuencia visual):
///
/// 1. **Contorno `EOR | ONEDOT`** y **aristas horizontales descartadas** (`y0 == y1`):
///    una horizontal no aporta cruce y metería un píxel extra en los vértices.
/// 2. **`BLTDPT` = base del bitmap, REESCRITO EN CADA LÍNEA** (el «punto inofensivo»
///    al que va el primer píxel por D): mantiene la paridad PAR/IMPAR del contorno en
///    los vértices. **NO hoisteable al preludio**: en modo línea el Blitter MUTA el
///    puntero D durante el blit (`WinUAE-DBG/blitter.cpp:731-745`), así que fijarlo
///    una vez manda el primer píxel de las líneas siguientes a direcciones variables →
///    **rayas horizontales de ancho completo** (regresión real 2026-10-17; ver
///    `blitter_lines_begin` y la bitácora `OPTIMIZACION_GPP_68000.md`). Corolario: en
///    modo línea NO se puede solapar la programación con el blit en curso (los
///    registros se leen en vivo).
/// 3. **El area fill se lanza DESPUÉS del último contorno** (es su frontera): el orden
///    de llamadas es parte del contrato.
/// 4. Con `bbox != nullptr`, la caja debe cubrir **todo** el contorno con ≥2 px de
///    margen: la semilla y el corte del barrido acortado asumen filas limpias fuera.
/// 5. **Un solo fill global** (nunca troceado por plano: los jobs se serializan y la
///    semilla por plano cambia la propagación vertical — medido peor).
///
/// ```text
///   ┌─ relleno por contorno + area fill XOR (sólido convexo) ─────────────┐
///   │ 1. blitter_lines_begin      fija los comunes del modo línea      │
///   │ 2. por arista visible y plano:  blitter_line_draw (EOR, ONEDOT,  │
///   │                                 BLTDPTR = base del bitmap)           │
///   │    (las aristas horizontales se descartan)                           │
///   │ 3. blitter_area_fill (XOR ↓)    conmuta el interior por PARIDAD      │
///   └──────────────────────────────────────────────────────────────────────┘
///   color de arista = XOR de las luces de las caras adyacentes (las internas se cancelan)
///   Exige sólido CONVEXO (contorno cerrado); frágil → gate VISUAL (freeze-diff), no cobertura
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::retro {

/// Arista de contorno proyectada: extremos en pantalla y máscara de color (bit `p` = planos
/// en los que se dibuja la línea).
struct OutlineEdge {
	eng::s16 x0 = 0;
	eng::s16 y0 = 0;
	eng::s16 x1 = 0;
	eng::s16 y1 = 0;
	eng::u8 color = 0;
};

/// Dibuja un contorno convexo + area fill XOR sobre `planes`. Devuelve el número de
/// líneas-plano lanzadas (0 si no hay aristas). `area_fill_wait` decide si el area fill
/// espera al Blitter (por defecto sí); con `false` se encola y el llamador lo sincroniza
/// más tarde (solape con la CPU, patrón de la pipeline de 3 buffers). `plane_count` =
/// planos del color.
///
/// `bbox` (opcional, `{minX, maxX, minY, maxY}`) **acota el area fill** a la caja del
/// objeto (±2 px de margen, recortada al bitmap): el barrido XOR solo necesita cubrir el
/// contorno (todas las cruces están dentro), así que el fill DMA cae de todo el bitmap a
/// la caja del objeto. Con `nullptr` barre los planos completos (comportamiento histórico).
/// La paridad del relleno es idéntica dentro de la caja (fuera no había cruces).
///
/// `line_d_base` (opcional): palabra Chip de descarte para el primer píxel de cada línea
/// (el trazador en modo línea escribe el primer píxel por D). Con `nullptr` se usa la base
/// del bitmap: el píxel cae en la fila 0 y se ve como un punto suelto si esa fila está en
/// pantalla. Un puntero fijo y fuera de la zona visible lo absorbe sin alterar la paridad
/// (mismo truco que `m_line_trash` del backend).
///
/// `per_plane_fill` (solo con `bbox`): en vez de un fill multi-plano contiguo (que recorre
/// las `plane_count` imágenes completas y solo recorta filas), lanza **un fill por plano
/// recortado a la caja en ancho y alto**. Es la variante barata cuando el objeto ocupa una
/// fracción pequeña del bitmap (p. ej. 320×208): el trabajo cae de
/// `filas_totales × ancho_completo` a `plane_count × filas_caja × palabras_caja`. Los blits
/// por plano se serializan; por eso el llamador debe usar esta vía solo si el recorte
/// compensa (objetos pequeños), no para barridos de pantalla completa.
template <class Backend>
inline eng::u32 flat_shade_xor(Backend& backend, eng::PlaneBytes planes, eng::u16 row_bytes,
			       eng::u32 plane_bytes, eng::u8 plane_count, eng::u16 width,
			       eng::u16 height, eng::Span<const OutlineEdge> edges,
			       bool area_fill_wait = true, const eng::s16* bbox = nullptr,
			       bool per_plane_fill = false, eng::u8* line_d_base = nullptr) {
	eng::u8* base = planes.data();
	// `BLTDPT` NO se hoista al preludio (el modo línea lo muta durante el blit; el
	// primer píxel de cada línea debe reescribirse al punto inofensivo — ver
	// `blitter_lines_begin` y la regresión de rayas de 2026-10-17).
	backend.blitter_lines_begin(row_bytes);
	eng::u32 lines = 0u;
	for (eng::usize i = 0u; i < edges.size(); ++i) {
		const OutlineEdge& e = edges[i];
		// Horizontales fuera: no aportan cruce y ensucian los vértices (paridad).
		if (e.y0 == e.y1) {
			continue;
		}
		typename Backend::LineEorParams line {};
		if (!backend.blitter_line_eor_prepare(line, row_bytes, e.x0, e.y0, e.x1, e.y1)) {
			continue;
		}
		for (eng::u8 p = 0u; p < plane_count; ++p) {
			if ((e.color & (1u << p)) != 0u) {
				backend.blitter_line_draw(
					line, base + static_cast<eng::u32>(p) * plane_bytes,
					line_d_base != nullptr ? line_d_base : base);
				++lines;
			}
		}
	}
	if (bbox == nullptr) {
		backend.blitter_area_fill(planes, plane_count, row_bytes, plane_bytes, width,
					  height, area_fill_wait);
		return lines;
	}
	// Fill acotado: **UN solo blit contiguo** con el barrido acortado por la caja del
	// contorno (`blitter_area_fill(..., bbox)`): la semilla va bajo la caja (fila
	// maxY+2 del último plano) y el barrido muere tras la fila minY-2 del plano 0.
	// Las filas externas son limpias por contrato (la caja cubre TODO el contorno con
	// margen de 2 px) y una fila sin cruces reescribe D con su propio valor, así que
	// omitirlas deja la paridad y los píxeles intactos. Sustituye al intento de 4 jobs
	// por plano, medido peor (se serializaban dentro del update).
	if (per_plane_fill) {
		// Recorte también en ANCHO, plano a plano: cada fill toca solo las palabras de
		// la caja. El fill multi-plano contiguo no puede recortar ancho (la paridad
		// necesita la fila completa), así que paga `ancho_total × filas_totales`.
		eng::s16 x0 = bbox[0] - 2;
		eng::s16 y0 = bbox[2] - 2;
		eng::s16 x1 = bbox[1] + 2;
		eng::s16 y1 = bbox[3] + 2;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > static_cast<eng::s16>(width - 1u)) x1 = static_cast<eng::s16>(width - 1u);
		if (y1 > static_cast<eng::s16>(height - 1u)) y1 = static_cast<eng::s16>(height - 1u);
		if (x1 >= x0 && y1 >= y0) {
			const eng::u16 wx0 = static_cast<eng::u16>(x0) & 0xfff0u;
			const eng::u16 wx1 = static_cast<eng::u16>(x1) | 0x000fu;
			const eng::u16 words =
				static_cast<eng::u16>((static_cast<eng::u16>(wx1 - wx0) + 16u) >> 4);
			const eng::u16 rows = static_cast<eng::u16>(y1 - y0 + 1);
			eng::u8* base = planes.data();
			for (eng::u8 p = 0u; p < plane_count; ++p) {
				const eng::PlaneBytes plane {
					base + static_cast<eng::u32>(p) * plane_bytes, plane_bytes};
				backend.blitter_area_fill_rect(plane, row_bytes, wx0, y0, words, rows,
							       area_fill_wait &&
								       p + 1u == plane_count);
			}
		}
		return lines;
	}
	backend.blitter_area_fill(planes, plane_count, row_bytes, plane_bytes, width, height,
				  area_fill_wait, bbox);
	return lines;
}

} // namespace eng::retro
