#pragma once

/// \file polygon_planes.hpp
/// **Relleno de polígonos compuesto por bitplane** (la forma barata en Amiga).
///
/// El Blitter rellena **máscaras de 1 bit** (outline en modo línea + area fill); el color de
/// un polígono no es más que un patrón de bits en los planos. Por eso, en vez de rellenar
/// **polígono a polígono** (outline + fill + aplicar a los planos de su color), se puede
/// reorganizar el trabajo **por plano**:
///
/// ```text
///   para cada plano p (0..planes-1):
///       dibujar las aristas de TODAS las caras cuyo color tiene el bit p a 1
///       un solo fill de ese contorno -> máscara del plano p
///       escribir la máscara en el plano p del destino
/// ```
///
/// Las **aristas compartidas** por caras con el mismo bit se cancelan solas: se dibujan dos
/// veces (una por cara) y el relleno even-odd las cuenta dos veces (doble cruce → sin
/// frontera); donde los bits difieren se dibujan una vez (frontera real). Con muchos
/// polígonos adyacentes de pocos colores, el número de fills cae de
/// «N polígonos × planos afectados» a «≈ número de planos».
///
/// ```text
///   por polígono (caro)                      por PLANO (barato)
///   ───────────────────                      ──────────────────
///   cara → outline + fill + aplicar          plano p: aristas de TODAS las caras con el bit p = 1
///   a cada plano de su color                     → 1 fill → máscara del plano p → escribir el plano p
///   ≈ N · planos fills                       ≈ nº de planos fills
///   Aristas compartidas: mismo bit → se dibujan 2× y el even-odd las cancela (sin frontera);
///   bits distintos → 1 vez (frontera real).
/// ```
///
/// Esta es la **referencia CPU** (host-testable); el backend Amiga hace lo mismo con el
/// Blitter (línea + `area fill` + copia por plano) sin cambiar la API.

#include <eng/core/types/domains.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/graphics/fill_pattern.hpp>

namespace eng::graphics {

/// **Cara** para el relleno compuesto: vértices en pantalla (`s16`, cerrada: la arista
/// `n-1 -> 0` se añade sola) e **índice de color** (su patrón de bits reparte la cara entre
/// los planos). `color == 0` no rellena nada.
struct PlanePolygon {
	const eng::s16* xs = nullptr; ///< coordenadas x de los vértices
	const eng::s16* ys = nullptr; ///< coordenadas y de los vértices
	eng::u8 count = 0;            ///< nº de vértices (>= 3)
	eng::u8 color = 0;            ///< índice de color (bits = planos con 1)
};

namespace detail {

/// Escribe el span `[xl, xr]` (inclusivo, ya recortado) OR-eando los bits en la fila `y` del
/// plano contiguo, **enmascarados por la palabra de patrón** de esa fila (`0xFFFF` = sólido).
/// Palabra completa en el interior (también enmascarada); extremos con máscara doble.
inline void plane_or_span(eng::u8* plane, eng::u16 row_bytes, eng::s32 y, eng::s32 xl, eng::s32 xr,
			  eng::u16 pattern_word) {
	eng::u8* row = plane + static_cast<eng::u32>(y) * row_bytes;
	const eng::u32 b0 = static_cast<eng::u32>(xl) >> 3u;
	const eng::u32 b1 = static_cast<eng::u32>(xr) >> 3u;
	// MSB = píxel izquierdo: índice de byte par -> byte alto de la palabra de patrón.
	const eng::u8 pat_hi = static_cast<eng::u8>(pattern_word >> 8u);
	const eng::u8 pat_lo = static_cast<eng::u8>(pattern_word & 0xffu);
	const auto pat_at = [&](eng::u32 b) -> eng::u8 { return (b & 1u) != 0u ? pat_lo : pat_hi; };
	const eng::u8 m0 = static_cast<eng::u8>(0xffu >> (static_cast<eng::u32>(xl) & 7u));
	const eng::u8 m1 = static_cast<eng::u8>(0xffu << (7u - (static_cast<eng::u32>(xr) & 7u)));
	if (b0 == b1) {
		row[b0] = static_cast<eng::u8>(row[b0] | ((m0 & m1) & pat_at(b0)));
		return;
	}
	row[b0] = static_cast<eng::u8>(row[b0] | (m0 & pat_at(b0)));
	row[b1] = static_cast<eng::u8>(row[b1] | (m1 & pat_at(b1)));
	for (eng::u32 i = b0 + 1u; i < b1; ++i) {
		row[i] = static_cast<eng::u8>(row[i] | pat_at(i));
	}
}

/// Cruces de una scanline (capacidad fija, sin heap). Si se supera, se descarta el exceso.
inline constexpr eng::u8 kMaxCrossings = 128;

} // namespace detail

/// Arista proyectada del relleno por plano (extremos en pantalla; la orientación es
/// indiferente para el planificador de paridad).
struct PlaneEdge {
	eng::s16 x0 = 0;
	eng::s16 y0 = 0;
	eng::s16 x1 = 0;
	eng::s16 y1 = 0;
};

namespace detail {

/// Canoniza una arista por `(y0, x0) <= (y1, x1)` para que dos caras que comparten
/// arista la aporten idéntica (la dedupe de paridad compara tuplas exactas).
[[nodiscard]] inline PlaneEdge canon_edge(PlaneEdge e) {
	const bool swap = (e.y0 > e.y1) || (e.y0 == e.y1 && e.x0 > e.x1);
	if (swap) {
		const eng::s16 tx = e.x0, ty = e.y0;
		e.x0 = e.x1;
		e.y0 = e.y1;
		e.x1 = tx;
		e.y1 = ty;
	}
	return e;
}

/// Dedupe por multiplicidad: deja solo las aristas de multiplicidad IMPAR (una vez cada
/// una). Dibujar 2× con EOR equivale a no dibujar, así que el contorno resultante tiene
/// la misma paridad por fila con menos blits. `out[i].x0 = 0x7fff` marca consumidas.
[[nodiscard]] inline eng::u32 dedupe_plane_edges(PlaneEdge* out, eng::u32 e_count) {
	const auto same = [](PlaneEdge a, PlaneEdge b) {
		return a.x0 == b.x0 && a.y0 == b.y0 && a.x1 == b.x1 && a.y1 == b.y1;
	};
	eng::u32 written = 0u;
	for (eng::u32 i = 0u; i < e_count; ++i) {
		if (out[i].x0 == 0x7fff) {
			continue; // ya consumida como duplicado par
		}
		eng::u32 mult = 1u;
		for (eng::u32 j = i + 1u; j < e_count; ++j) {
			if (out[j].x0 == 0x7fff) {
				continue;
			}
			if (same(out[i], out[j])) {
				++mult;
				out[j].x0 = 0x7fff; // marca de consumida
			}
		}
		if ((mult & 1u) != 0u) {
			out[written++] = out[i];
		}
	}
	return written;
}

} // namespace detail

/// **Planifica las aristas que hay que dibujar en el plano `bit`** con paridad *even-odd*:
/// cada cara con ese bit aporta sus aristas una vez; las **compartidas** por dos caras del
/// mismo bit se dibujan dos veces y el EOR las cancela. Como «dibujar 2× ≡ no dibujar»,
/// el plan devuelve solo las aristas de **multiplicidad impar, una vez cada una**: mismo
/// contorno (misma paridad por fila) con menos blits de línea. Devuelve el número de
/// aristas escritas en `out`, o `cap + 1` si no caben (el llamador dibuja todas, como sin
/// plan). `bit` = plano (0..7). Las aristas horizontales se incluyen en el recuento (el
/// trazador EOR las descarta después por diseño).
///
/// **No garantiza que las coordenadas estén dentro del bitmap**: para el Blitter usa
/// `plan_plane_edges_in_bounds` (recorta cada cara antes de aportar sus aristas).
[[nodiscard]] inline eng::u32 plan_plane_edges(const PlanePolygon* faces, eng::u32 n, eng::u8 bit,
					       PlaneEdge* out, eng::u32 cap) {
	if (faces == nullptr || out == nullptr) {
		return cap + 1u;
	}
	eng::u32 total = 0u;
	for (eng::u32 f = 0u; f < n; ++f) {
		if ((faces[f].color & (1u << bit)) == 0u || faces[f].count < 3u) {
			continue;
		}
		total += faces[f].count;
	}
	if (total > cap) {
		return cap + 1u;
	}
	eng::u32 e_count = 0u;
	for (eng::u32 f = 0u; f < n; ++f) {
		const PlanePolygon& face = faces[f];
		if ((face.color & (1u << bit)) == 0u || face.count < 3u) {
			continue;
		}
		for (eng::u8 i = 0u; i < face.count; ++i) {
			const eng::u8 j = static_cast<eng::u8>((i + 1u) % face.count);
			out[e_count] = detail::canon_edge(
				PlaneEdge {face.xs[i], face.ys[i], face.xs[j], face.ys[j]});
			++e_count;
		}
	}
	return detail::dedupe_plane_edges(out, e_count);
}

/// **Recorta una cara al rectángulo de píxeles** `[min_x, max_x] × [min_y, max_y]`
/// (Sutherland–Hodgman contra los cuatro bordes, sin heap). `out_xs`/`out_ys` deben tener
/// capacidad ≥ `n + 4` (un convexo gana como mucho un vértice por plano de recorte;
/// con caras de hasta 16 vértices, 20 sobra). Devuelve el número de vértices (0 si no
/// queda nada visible).
///
/// **Por qué es obligatorio antes del Blitter** (ver
/// `docs/reference/amiga/techniques/blitter-line-subpixel-fill.md` §2 y las sondas 085/
/// L1-003): el trazador de líneas escribe cada píxel **un paso por detrás** de la
/// posición que lee (el primer píxel va por D y el resto por C, que ya avanzo), así que
/// una vertical dada en `x` se dibuja en `x-1` (y sus filas, un paso más abajo). Por eso:
/// un vértice con `x == width` (el borde del frustum, que la proyección deja a ±1 px del
/// viewport) deja el contorno en la **palabra siguiente de la fila** y uno con `x == 0`
/// lo deja en `x == -1`, que por el complemento a dos cae en la **cola de la fila
/// anterior** (x≈width-1). El contorno queda fuera del bbox de la cara y el clear por
/// rectángulo sucio no lo borra nunca en los frames siguientes (residuo).
///
/// El rectángulo de contorno debe ser por tanto **[1, width-1] × [0, height-1]**: el
/// margen izquierdo de 1 px absorbe el desfase del trazador y deja la columna 0 sin
/// pintar por el contorno (borde seguro de 1 px). El último píxel **incluido** sigue
/// siendo `width-1` (el trazador lo dibuja en `width-2`, dentro del bitmap) y el borde
/// inferior `height-1` (no escribe la fila `height`).
template <class S = eng::pix>
[[nodiscard]] inline eng::u8 clip_face_to_rect(const S* xs, const S* ys, eng::u8 n, S min_x,
					       S max_x, S min_y, S max_y, S* out_xs, S* out_ys) {
	if (xs == nullptr || ys == nullptr || out_xs == nullptr || out_ys == nullptr || n < 3u) {
		return 0u;
	}
	if (n > 16u) {
		return 0u;
	}
	// Coordenadas acotadas antes de interpolar: los productos de los cruces son
	// `s32` y así no desbordan ni con basura de un llamador.
	const auto sat = [](S v) -> S {
		constexpr S lim = 16384;
		if (v > lim) return lim;
		if (v < -lim) return -lim;
		return v;
	};
	// Buffer A: entrada saneada; A/B alternan como entrada/salida de los cuatro recortes.
	S ax[20] {}, ay[20] {}, bx[20] {}, by[20] {};
	eng::u8 in_n = n;
	for (eng::u8 i = 0u; i < n; ++i) {
		ax[i] = sat(xs[i]);
		ay[i] = sat(ys[i]);
	}
	S* cx = ax;
	S* cy = ay;
	S* nx = bx;
	S* ny = by;
	// Cuatro semiplanos: x >= min_x, x <= max_x, y >= min_y, y <= max_y. `is_y` elige el
	// eje, `keep_high` el lado interior (>= / <=) y `limit` el valor del borde.
	for (eng::u8 plane = 0u; plane < 4u; ++plane) {
		if (in_n < 3u) {
			return 0u;
		}
		const bool is_y = plane >= 2u;
		const bool keep_high = (plane == 0u || plane == 2u);
		eng::s32 limit = 0;
		if (plane == 1u) {
			limit = static_cast<eng::s32>(max_x);
		} else if (plane == 2u) {
			limit = static_cast<eng::s32>(min_y);
		} else if (plane == 3u) {
			limit = static_cast<eng::s32>(max_y);
		} else {
			limit = static_cast<eng::s32>(min_x);
		}
		eng::u8 out_n = 0u;
		for (eng::u8 i = 0u; i < in_n; ++i) {
			// Sin `%`: la arista de cierre es n-1 -> 0 (este bucle es caliente).
			const eng::u8 j = static_cast<eng::u8>((i + 1u == in_n) ? 0u : (i + 1u));
			const eng::s32 p = static_cast<eng::s32>(is_y ? cy[i] : cx[i]);
			const eng::s32 q = static_cast<eng::s32>(is_y ? cy[j] : cx[j]);
			const bool pin = keep_high ? (p >= limit) : (p <= limit);
			const bool qin = keep_high ? (q >= limit) : (q <= limit);
			// Un vértice interior se copia TAL CUAL; solo el cruce se apoya en el borde
			// (`limit`), de modo que el contorno del borde cae en el píxel incluido
			// (width-1) y nunca en la palabra siguiente.
			if (pin) {
				nx[out_n] = cx[i];
				ny[out_n] = cy[i];
				++out_n;
			}
			if (pin != qin) {
				// Cruce: se interpola la OTRA coordenada en el borde (una división por
				// arista cortada; un convexo corta como mucho 2 veces por plano). Canonizar
				// dentro→fuera hace idénticos los cortes de caras vecinas que comparten una
				// arista con winding opuesto; sin ello la truncación entera abre una costura
				// para el contorno EOR (`../WinUAE-DBG/blitter.cpp:274-285,913-919`; AHRM 3.ª ed.,
				// cap. 6, «Area Fill Mode», p. 184).
				const eng::s32 inside_axis = pin ? p : q;
				const eng::s32 outside_axis = pin ? q : p;
				const eng::s32 inside_other = static_cast<eng::s32>(is_y ? (pin ? cx[i] : cx[j])
										 : (pin ? cy[i] : cy[j]));
				const eng::s32 outside_other = static_cast<eng::s32>(is_y ? (pin ? cx[j] : cx[i])
										  : (pin ? cy[j] : cy[i]));
				const eng::s32 den = outside_axis - inside_axis;
				eng::s32 v = inside_other;
				if (den != 0) {
					v = inside_other + static_cast<eng::s32>(
								   (static_cast<eng::s32>(limit) - inside_axis) *
								   (outside_other - inside_other) / den);
				}
				if (v < -16384) v = -16384;
				if (v > 16384) v = 16384;
				nx[out_n] = is_y ? static_cast<S>(v) : static_cast<S>(limit);
				ny[out_n] = is_y ? static_cast<S>(limit) : static_cast<S>(v);
				++out_n;
			}
			if (out_n >= 20u) {
				return 0u;
			}
		}
		in_n = out_n;
		// Swap de punteros: la salida pasa a ser la entrada del siguiente plano.
		S* t = cx; cx = nx; nx = t;
		t = cy; cy = ny; ny = t;
	}
	for (eng::u8 i = 0u; i < in_n; ++i) {
		out_xs[i] = cx[i];
		out_ys[i] = cy[i];
	}
	return in_n;
}

/// **Planifica el contorno del plano `bit` recortando cada cara al rectángulo seguro**
/// `[min_x, max_x] × [min_y, max_y]` (variante segura para el Blitter de
/// `plan_plane_edges`). Es la que debe usar cualquier camino que dibuje el contorno con
/// el Blitter: además de no escribir fuera del bitmap, el recorte **cierra** el contorno
/// en el borde (el `FILL_XOR` necesita paridad par por fila dentro de la caja). El
/// llamador pasa `min_x = 1` por el desfase de un píxel del trazador de líneas (ver
/// `clip_face_to_rect`). Devuelve el nº de aristas escritas o `cap + 1` si no caben
/// (el llamador debe dibujarlas igualmente, recortando cara a cara).
[[nodiscard]] inline eng::u32 plan_plane_edges_in_bounds(const PlanePolygon* faces, eng::u32 n,
							eng::u8 bit, eng::s16 min_x, eng::s16 max_x,
							eng::s16 min_y, eng::s16 max_y,
							PlaneEdge* out, eng::u32 cap) {
	if (faces == nullptr || out == nullptr) {
		return cap + 1u;
	}
	eng::u32 e_count = 0u;
	eng::s16 cx[20] {};
	eng::s16 cy[20] {};
	for (eng::u32 f = 0u; f < n; ++f) {
		const PlanePolygon& face = faces[f];
		if ((face.color & (1u << bit)) == 0u || face.count < 3u) {
			continue;
		}
		const eng::u8 m = clip_face_to_rect(face.xs, face.ys, face.count, min_x, max_x, min_y,
						    max_y, cx, cy);
		if (m < 3u) {
			continue;
		}
		if (e_count + m > cap) {
			return cap + 1u;
		}
		for (eng::u8 i = 0u; i < m; ++i) {
			const eng::u8 j = static_cast<eng::u8>((i + 1u == m) ? 0u : (i + 1u));
			out[e_count++] = detail::canon_edge(PlaneEdge {cx[i], cy[i], cx[j], cy[j]});
		}
	}
	return detail::dedupe_plane_edges(out, e_count);
}


/// rasteriza por **scanline even-odd** la unión de las caras cuyo `color` tiene el bit `p`
/// a 1 y escribe los spans en el plano `p` de `dest` (contiguo, `plane_bytes` por plano),
/// **enmascarados por `pattern`** (el mismo para todos los planos de la llamada; `Solid` =
/// sin patrón). Los planos destino se **OR-ean** (el llamador los limpia antes). Devuelve el
/// nº de spans escritos. `width`/`height` recortan. Es el equivalente CPU del fill por
/// Blitter (1 fill por plano) y sirve para validar la composición sin hardware.
inline eng::u32 fill_polygons_by_plane_cpu(eng::Span<const PlanePolygon> faces,
					   eng::PlaneBytes dest, eng::u16 row_bytes,
					   eng::u32 plane_bytes, eng::u8 planes,
					   eng::u16 width, eng::u16 height,
					   FillPattern pattern = FillPattern::Solid);

/// Variante con **patrón por plano** (`patterns[p]`, `planes` entradas): la forma que pide
/// la **mezcla de dos índices con damero a nivel de píxel** (el damero y su inverso
/// repartidos por plano según los bits de los dos colores). Mismos planos OR-eados.
inline eng::u32 fill_polygons_by_plane_patterns_cpu(eng::Span<const PlanePolygon> faces,
						    eng::PlaneBytes dest, eng::u16 row_bytes,
						    eng::u32 plane_bytes, eng::u8 planes,
						    eng::u16 width, eng::u16 height,
						    eng::Span<const FillPattern> patterns) {
	if (faces.empty() || dest.empty() || planes == 0u || row_bytes == 0u || plane_bytes == 0u) {
		return 0u;
	}
	// **Active edge list** con x en 16.16: UNA división por arista (pendiente) y sumas
	// por fila. El scanline ingenuo dividía por cruce y por fila (~5.5k `__divsi3`/frame
	// en la 088: 1.14 fps); esta versión mantiene el mismo rasterizado even-odd.
	struct CpuEdge {
		eng::s32 x_q16 = 0;  // x en 16.16 al inicio de [y0, y1)
		eng::s32 dx_q16 = 0; // pendiente en 16.16
		eng::s16 y0 = 0;
		eng::s16 y1 = 0;
	};
	constexpr eng::u32 kMaxEdges = 128u;
	CpuEdge edges[kMaxEdges];
	eng::s32 cross[detail::kMaxCrossings];
	eng::u32 spans = 0u;
	const eng::s32 h = static_cast<eng::s32>(height);
	const eng::s32 wlast = static_cast<eng::s32>(width) - 1;
	for (eng::u8 p = 0; p < planes; ++p) {
		eng::u8* plane = dest.data() + static_cast<eng::u32>(p) * plane_bytes;
		// 1) Aristas de las caras de este plano, normalizadas a y0 < y1 y recortadas.
		eng::u32 ne = 0u;
		for (eng::u32 f = 0; f < faces.size(); ++f) {
			const PlanePolygon& face = faces[f];
			if ((face.color & (1u << p)) == 0u || face.count < 3u) {
				continue;
			}
			for (eng::u8 i = 0; i < face.count; ++i) {
				const eng::u8 j = static_cast<eng::u8>((i + 1u == face.count) ? 0u : (i + 1u));
				eng::s32 ya = face.ys[i];
				eng::s32 yb = face.ys[j];
				if (ya == yb) {
					continue; // horizontal: no cruza filas
				}
				eng::s32 xa = face.xs[i];
				eng::s32 xb = face.xs[j];
				if (ya > yb) {
					const eng::s32 ty = ya;
					ya = yb;
					yb = ty;
					const eng::s32 tx = xa;
					xa = xb;
					xb = tx;
				}
				if (yb <= 0 || ya >= h) {
					continue;
				}
				if (ya < 0) {
					// Cruce con y=0 (una división por arista recortada).
					xa += (xb - xa) * (0 - ya) / (yb - ya);
					ya = 0;
				}
				if (yb > h) {
					// Recorta el extremo inferior moviendo TAMBIEN x (si no, la pendiente
					// se calcula contra el punto original y el span se pierde).
					xb = xa + (xb - xa) * (h - ya) / (yb - ya);
					yb = h;
				}
				const eng::s32 dy = yb - ya;
				edges[ne].x_q16 = xa << 16;
				edges[ne].dx_q16 = ((xb - xa) << 16) / dy;
				edges[ne].y0 = static_cast<eng::s16>(ya);
				edges[ne].y1 = static_cast<eng::s16>(yb);
				++ne;
				if (ne >= kMaxEdges) {
					break;
				}
			}
			if (ne >= kMaxEdges) {
				break;
			}
		}
		// 2) Barrido: cada arista avanza su x solo mientras está activa (semiaabierto).
		for (eng::s32 y = 0; y < h; ++y) {
			eng::u32 n = 0u;
			for (eng::u32 e = 0u; e < ne; ++e) {
				CpuEdge& ed = edges[e];
				if (y < ed.y0 || y >= ed.y1) {
					continue;
				}
				if (n >= detail::kMaxCrossings) {
					break;
				}
				cross[n++] = ed.x_q16 >> 16;
				ed.x_q16 += ed.dx_q16;
			}
			// Inserción: n es pequeño (cruces de una scanline).
			for (eng::u32 i = 1u; i < n; ++i) {
				const eng::s32 key = cross[i];
				eng::u32 k = i;
				while (k > 0u && cross[k - 1u] > key) {
					cross[k] = cross[k - 1u];
					--k;
				}
				cross[k] = key;
			}
			for (eng::u32 k = 0u; k + 1u < n; k += 2u) {
				eng::s32 xl = cross[k];
				eng::s32 xr = cross[k + 1u] - 1; // even-odd: [xl, xr)
				if (xl < 0) xl = 0;
				if (xr > wlast) xr = wlast;
				if (xr < xl) {
					continue;
				}
				detail::plane_or_span(plane, row_bytes, y, xl, xr,
						      fill_pattern_word(
							      p < patterns.size()
								      ? patterns[p]
								      : FillPattern::Solid,
							      static_cast<eng::u32>(y)));
				++spans;
			}
		}
	}
	return spans;
}

/// Definición de la variante de un solo patrón (mismo patrón en todos los planos): delega
/// en la de patrones por plano.
inline eng::u32 fill_polygons_by_plane_cpu(eng::Span<const PlanePolygon> faces,
					   eng::PlaneBytes dest, eng::u16 row_bytes,
					   eng::u32 plane_bytes, eng::u8 planes,
					   eng::u16 width, eng::u16 height, FillPattern pattern) {
	if (planes == 0u || planes > 8u) {
		return 0u;
	}
	FillPattern arr[8] = {};
	for (eng::u8 p = 0u; p < planes; ++p) {
		arr[p] = pattern;
	}
	return fill_polygons_by_plane_patterns_cpu(
		faces, dest, row_bytes, plane_bytes, planes, width, height,
		eng::Span<const FillPattern>(arr, planes));
}

/// **Acumulador de caras** para el relleno por bitplane (patrón `SubmitPoly`/`EndFrame`):
/// el llamador **posee los vértices** (el builder guarda solo las vistas, sin heap) y, al
/// cerrar el frame, se rellena por plano (CPU o Blitter) con una sola pasada.
///
/// ```cpp
/// PlaneFillBuilder<64> fb;
/// fb.submit(xs, ys, n, color);      // ... por cada polígono (no dibuja aún)
/// fb.fill_cpu(dest, row_bytes, plane_bytes, planes, width, height);  // EndFrame
/// // o, en el backend: backend.fill_polygons_by_plane(fb.faces().data(), fb.count(), ...);
/// ```
template <eng::u16 MaxFaces>
class PlaneFillBuilder {
public:
	/// Añade un polígono (vértices en pantalla + color). `false` si está lleno o `count < 3`.
	bool submit(const eng::s16* xs, const eng::s16* ys, eng::u8 count, eng::u8 color) {
		if (m_count >= MaxFaces || xs == nullptr || ys == nullptr || count < 3u) {
			return false;
		}
		m_faces[m_count++] = PlanePolygon {xs, ys, count, color};
		return true;
	}

	/// Rellena por CPU los planos de `dest` (ver `fill_polygons_by_plane_cpu`).
	eng::u32 fill_cpu(eng::PlaneBytes dest, eng::u16 row_bytes, eng::u32 plane_bytes,
			  eng::u8 planes, eng::u16 width, eng::u16 height,
			  FillPattern pattern = FillPattern::Solid) const {
		return fill_polygons_by_plane_cpu({m_faces.data(), m_count},
						  dest, row_bytes, plane_bytes, planes, width, height,
						  pattern);
	}

	/// Caras acumuladas (para el camino Blitter del backend).
	[[nodiscard]] eng::Span<const PlanePolygon> faces() const {
		return eng::Span<const PlanePolygon> {m_faces.data(), m_count};
	}
	[[nodiscard]] eng::u16 count() const { return m_count; }
	void reset() { m_count = 0u; }

private:
	eng::util::Array<PlanePolygon, MaxFaces> m_faces {};
	eng::u16 m_count = 0u;
};

} // namespace eng::graphics
