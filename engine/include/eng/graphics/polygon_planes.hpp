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
/// plano contiguo. Palabra completa en el interior; extremos con máscara.
inline void plane_or_span(eng::u8* plane, eng::u16 row_bytes, eng::s32 y, eng::s32 xl, eng::s32 xr) {
	eng::u8* row = plane + static_cast<eng::u32>(y) * row_bytes;
	const eng::u32 b0 = static_cast<eng::u32>(xl) >> 3u;
	const eng::u32 b1 = static_cast<eng::u32>(xr) >> 3u;
	const eng::u8 first = static_cast<eng::u8>(0xffu >> (static_cast<eng::u32>(xl) & 7u));
	const eng::u8 last = static_cast<eng::u8>(0xffu << (7u - (static_cast<eng::u32>(xr) & 7u)));
	if (b0 == b1) {
		row[b0] = static_cast<eng::u8>(row[b0] | (first & last));
		return;
	}
	row[b0] = static_cast<eng::u8>(row[b0] | first);
	row[b1] = static_cast<eng::u8>(row[b1] | last);
	for (eng::u32 i = b0 + 1u; i < b1; ++i) {
		row[i] = 0xffu;
	}
}

/// Cruces de una scanline (capacidad fija, sin heap). Si se supera, se descarta el exceso.
inline constexpr eng::u8 kMaxCrossings = 128;

} // namespace detail

/// **Rellena un conjunto de caras componiendo por bitplane (CPU)**: para cada plano `p`,
/// rasteriza por **scanline even-odd** la unión de las caras cuyo `color` tiene el bit `p`
/// a 1 y escribe los spans en el plano `p` de `dest` (contiguo, `plane_bytes` por plano).
/// Los planos destino se **OR-ean** (el llamador los limpia antes). Devuelve el nº de spans
/// escritos. `width`/`height` recortan. Es el equivalente CPU del fill por Blitter
/// (1 fill por plano) y sirve para validar la composición sin hardware.
inline eng::u32 fill_polygons_by_plane_cpu(eng::Span<const PlanePolygon> faces,
					   eng::PlaneBytes dest, eng::u16 row_bytes,
					   eng::u32 plane_bytes, eng::u8 planes,
					   eng::u16 width, eng::u16 height) {
	if (faces.empty() || dest.empty() || planes == 0u || row_bytes == 0u || plane_bytes == 0u) {
		return 0u;
	}
	eng::s32 cross[detail::kMaxCrossings];
	eng::u32 spans = 0u;
	for (eng::u8 p = 0; p < planes; ++p) {
		eng::u8* plane = dest.data() + static_cast<eng::u32>(p) * plane_bytes;
		for (eng::s32 y = 0; y < static_cast<eng::s32>(height); ++y) {
			eng::u32 n = 0u;
			for (eng::u32 f = 0; f < faces.size(); ++f) {
				const PlanePolygon& face = faces[f];
				if ((face.color & (1u << p)) == 0u || face.count < 3u) {
					continue;
				}
				for (eng::u8 i = 0; i < face.count; ++i) {
					const eng::u8 j = static_cast<eng::u8>((i + 1u) % face.count);
					const eng::s32 y0 = face.ys[i];
					const eng::s32 y1 = face.ys[j];
					// Semiaabierta [y0, y1): un vértice compartido cuenta una vez.
					const bool up = (y0 <= y) && (y < y1);
					const bool down = (y1 <= y) && (y < y0);
					if (!up && !down) {
						continue;
					}
					if (n >= detail::kMaxCrossings) {
						break;
					}
					const eng::s32 x0 = face.xs[i];
					const eng::s32 x1 = face.xs[j];
					cross[n++] = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
				}
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
				if (xr >= static_cast<eng::s32>(width)) xr = static_cast<eng::s32>(width) - 1;
				if (xr < xl) {
					continue;
				}
				detail::plane_or_span(plane, row_bytes, y, xl, xr);
				++spans;
			}
		}
	}
	return spans;
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
			  eng::u8 planes, eng::u16 width, eng::u16 height) const {
		return fill_polygons_by_plane_cpu({m_faces.data(), m_count},
						  dest, row_bytes, plane_bytes, planes, width, height);
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
