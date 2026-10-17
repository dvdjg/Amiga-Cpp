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
	backend.blitter_lines_begin(row_bytes);
	eng::u8* base = planes.data();
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
	// Fill acotado por cara: una caja con TODAS las cruces del contorno basta para que la
	// paridad rellene igual que el barrido completo (no hay cruces fuera).
	eng::s16 x0 = static_cast<eng::s16>(bbox[0] - 2);
	eng::s16 y0 = static_cast<eng::s16>(bbox[2] - 2);
	eng::s16 x1 = static_cast<eng::s16>(bbox[1] + 2);
	eng::s16 y1 = static_cast<eng::s16>(bbox[3] + 2);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	const eng::s16 max_x = static_cast<eng::s16>(width - 1u);
	const eng::s16 max_y = static_cast<eng::s16>(height - 1u);
	if (x1 > max_x) x1 = max_x;
	if (y1 > max_y) y1 = max_y;
	if (x1 < x0 || y1 < y0) {
		return lines;
	}
	const eng::u16 wx0 = static_cast<eng::u16>(x0) & 0xfff0u;
	const eng::u16 wx1 = static_cast<eng::u16>(x1) | 0x000fu;
	const eng::u16 words = static_cast<eng::u16>((static_cast<eng::u16>(wx1 - wx0) + 16u) >> 4u);
	const eng::u16 rows = static_cast<eng::u16>(y1 - y0 + 1);
	for (eng::u8 p = 0u; p < plane_count; ++p) {
		eng::PlaneBytes plane {
			base + static_cast<eng::u32>(p) * plane_bytes, plane_bytes};
		// Cada fill espera internamente al anterior (programa los registros con el
		// Blitter libre); el `area_fill_wait` solo gobierna la espera FINAL, igual que en
		// el barrido completo.
		backend.blitter_area_fill_rect(plane, row_bytes, wx0, y0, words, rows,
					       area_fill_wait);
	}
	return lines;
}

} // namespace eng::retro
