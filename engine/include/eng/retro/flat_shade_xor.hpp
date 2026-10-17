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
template <class Backend>
inline eng::u32 flat_shade_xor(Backend& backend, eng::PlaneBytes planes, eng::u16 row_bytes,
			       eng::u32 plane_bytes, eng::u8 plane_count, eng::u16 width,
			       eng::u16 height, eng::Span<const OutlineEdge> edges,
			       bool area_fill_wait = true, const eng::s16* bbox = nullptr) {
	eng::u8* base = planes.data();
	// `base` como `BLTDPT` fijo del lote: el truco del original (D siempre en la base
	// del bitmap) y los draws se ahorran esa escritura por linea-plano.
	backend.blitter_lines_begin(row_bytes, base);
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
					line, base + static_cast<eng::u32>(p) * plane_bytes, base);
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
	backend.blitter_area_fill(planes, plane_count, row_bytes, plane_bytes, width, height,
				  area_fill_wait, bbox);
	return lines;
}

} // namespace eng::retro
