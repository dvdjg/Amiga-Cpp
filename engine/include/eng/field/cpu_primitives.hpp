#pragma once

/// \file cpu_primitives.hpp
/// **Primitivas CPU optimizadas** (`eng::field`, portables): relleno de rectángulos, líneas y
/// polígonos por **tramos horizontales** (`Playfield::draw_span`), que ya agrupa 16/32 píxeles por
/// escritura. Evita el dibujo píxel a píxel (`write_pixel`) que es demasiado lento para uso real y
/// sirve también a plataformas sin Blitter (p. ej. el futuro port a Atari ST).
///
/// - **Rectángulo**: `cpu_fill_rect` — un `draw_span` por fila (no píxel a píxel).
/// - **Línea**: `cpu_line` — *run-slice*: por cada fila del recorrido se calcula el intervalo
///   contiguo de x y se pinta con un `draw_span`. El nº de spans es `min(|dx|,|dy|)+1`, no el nº
///   de píxeles.
/// - **Polígono**: `cpu_fill_polygon` — *edge table* + barrido por filas con `draw_span`
///   (even-odd), sin recorrer cada arista por fila.
///
/// Todas operan sobre `Playfield` (layout contiguo o interleaved) y no dependen del chipset: son
/// el *fallback* CPU y la referencia canónica frente a la ruta Blitter (ver
/// `docs/guides/methodology/PROTOCOLO_ETAPAS_GRAFICOS.md`, etapa 4).

#include <eng/core/types/types.hpp>
#include <eng/field/playfield_base.hpp>

namespace eng::field {

/// Rellena el rectángulo `[x, x+w) × [y, y+h)` con `color` por **spans** (una fila = un tramo).
/// Recorta contra los límites del playfield. Devuelve el nº de filas pintadas.
inline eng::u32 cpu_fill_rect(Playfield& pf, eng::s32 x, eng::s32 y, eng::u16 w, eng::u16 h,
			      eng::u8 color) {
	if (!pf.initialized() || w == 0u || h == 0u) {
		return 0u;
	}
	const eng::s32 x0 = x;
	const eng::s32 x1 = static_cast<eng::s32>(x) + static_cast<eng::s32>(w) - 1;
	eng::u32 rows = 0u;
	for (eng::s32 gy = y; gy < static_cast<eng::s32>(y) + static_cast<eng::s32>(h); ++gy) {
		if (gy < 0 || static_cast<eng::u32>(gy) >= pf.height()) {
			continue;
		}
		pf.draw_span(x0, x1, gy, color);
		++rows;
	}
	return rows;
}

/// Dibuja una línea `(x0,y0)-(x1,y1)` con **Bresenham agrupado en spans por fila**: se recorre
/// como Bresenham (misma cobertura de píxeles), pero los píxeles consecutivos de la **misma fila**
/// se pintan con un solo `Playfield::draw_span` en lugar de uno a uno. Para líneas mayormente
/// horizontales esto reduce de `|dx|` escrituras a `|dy|+1`. Recortado a la altura del playfield.
/// Devuelve el nº de tramos (filas) pintados.
inline eng::u32 cpu_line(Playfield& pf, eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1,
			 eng::u8 color) {
	if (!pf.initialized()) {
		return 0u;
	}
	const eng::s32 dx = x1 > x0 ? x1 - x0 : x0 - x1;
	const eng::s32 dy = y1 > y0 ? y1 - y0 : y0 - y1;
	const eng::s32 sx = x0 < x1 ? 1 : -1;
	const eng::s32 sy = y0 < y1 ? 1 : -1;
	eng::s32 err = dx - dy;
	// Estado del tramo en curso (fila actual y rango de x contiguo).
	eng::s32 run_y = y0;
	eng::s32 run_x0 = x0;
	eng::s32 run_x1 = x0;
	eng::u32 spans = 0u;
	for (;;) {
		if (y0 != run_y) {
			// Cambió la fila: pinta el tramo acumulado y empieza el nuevo.
			pf.draw_span(run_x0, run_x1, run_y, color);
			++spans;
			run_y = y0;
			run_x0 = x0;
			run_x1 = x0;
		} else if (x0 < run_x0) {
			run_x0 = x0;
		} else if (x0 > run_x1) {
			run_x1 = x0;
		}
		if (x0 == x1 && y0 == y1) {
			break;
		}
		const eng::s32 e2 = 2 * err;
		if (e2 > -dy) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dx) {
			err += dx;
			y0 += sy;
		}
	}
	pf.draw_span(run_x0, run_x1, run_y, color);
	++spans;
	return spans;
}

/// Rellena un polígono convexo dado por `n` vértices (`xs`,`ys`) con **edge table** y barrido por
/// filas (even-odd, regla top-left mínima), pintando cada tramo con `draw_span`. `n <= 16`.
/// Devuelve el nº de filas pintadas. No recorta el polígono (el llamador ya lo recortó al clip).
inline eng::u32 cpu_fill_polygon(Playfield& pf, const eng::s16* xs, const eng::s16* ys, eng::u8 n,
				 eng::u8 color) {
	constexpr eng::u8 kMaxV = 16u;
	if (!pf.initialized() || xs == nullptr || ys == nullptr || n < 3u || n > kMaxV) {
		return 0u;
	}
	// Rango vertical del polígono.
	eng::s32 ymin = ys[0], ymax = ys[0];
	eng::s32 xmin = xs[0], xmax = xs[0];
	for (eng::u8 i = 1u; i < n; ++i) {
		if (ys[i] < ymin) ymin = ys[i];
		if (ys[i] > ymax) ymax = ys[i];
		if (xs[i] < xmin) xmin = xs[i];
		if (xs[i] > xmax) xmax = xs[i];
	}
	if (ymin < 0) ymin = 0;
	if (static_cast<eng::u32>(ymax) >= pf.height()) {
		ymax = static_cast<eng::s32>(pf.height()) - 1;
	}
	if (ymax < ymin) {
		return 0u;
	}
	eng::u32 rows = 0u;
	for (eng::s32 y = ymin; y <= ymax; ++y) {
		// Intersecciones de las aristas con la fila y (even-odd). Máximo n/2 por fila.
		eng::s32 xints[kMaxV];
		eng::u8 m = 0u;
		for (eng::u8 i = 0u; i < n; ++i) {
			const eng::u8 j = static_cast<eng::u8>((i + 1u) % n);
			const eng::s32 ya = ys[i], yb = ys[j];
			// Regla: incluir la arista si [min, max) contiene y (evita doble conteo en vértices).
			const eng::s32 lo = ya < yb ? ya : yb;
			const eng::s32 hi = ya < yb ? yb : ya;
			if (y < lo || y >= hi) {
				continue;
			}
			const eng::s32 xa = xs[i], xb = xs[j];
			const eng::s32 xint = static_cast<eng::s32>(
				xa + static_cast<eng::s32>((xb - xa) * (y - ya) / (yb - ya)));
			if (m < kMaxV) {
				xints[m++] = xint;
			}
		}
		if (m < 2u) {
			continue;
		}
		// Ordena las intersecciones (pocas: inserción) y pinta pares (even-odd).
		for (eng::u8 a = 1u; a < m; ++a) {
			const eng::s32 v = xints[a];
			eng::u8 b = a;
			while (b > 0u && xints[b - 1u] > v) {
				xints[b] = xints[b - 1u];
				--b;
			}
			xints[b] = v;
		}
		for (eng::u8 k = 0u; k + 1u < m; k += 2u) {
			pf.draw_span(xints[k], static_cast<eng::s32>(xints[k + 1u]) - 1, y, color);
		}
		++rows;
	}
	return rows;
}

} // namespace eng::field
