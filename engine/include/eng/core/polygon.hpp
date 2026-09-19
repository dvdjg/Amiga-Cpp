#pragma once

/// \file polygon.hpp
/// **Generación de spans de un polígono convexo** en pantalla: el polígono se recorre por
/// **dos cadenas** (izquierda/derecha) desde el vértice superior al inferior, y por cada
/// scanline se emite `(y, xl, xr)`. Es O(altura) frente a O(lados·altura) del barrido que
/// recalcula el mínimo/máximo sobre todas las aristas en cada fila. Es la forma amiga del
/// raster Amiga (rellena polígonos convexos): el llamador escribe cada span como quiera
/// (píxel a píxel, un `write_span`, o un blit).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::math3d {

/// Genera los **spans** horizontales `(y, xl, xr)` de un polígono **convexo** por dos
/// cadenas. `emit(y, xl, xr)` se llama una vez por scanline (con `xl <= xr`; la fila
/// inferior queda semiaabierta, como el barrido de referencia). Los vértices deben estar en
/// orden de giro y formar un polígono convexo.
template <class Emit>
inline u32 convex_spans(Span<const s32> xs, Span<const s32> ys, Emit&& emit) {
	const u32 n = static_cast<u32>(xs.size());
	if (n < 3u || ys.size() != xs.size()) {
		return 0;
	}
	u32 top = 0;
	u32 bot = 0;
	for (u32 i = 1; i < n; ++i) {
		if (ys[i] < ys[top]) {
			top = i;
		}
		if (ys[i] > ys[bot]) {
			bot = i;
		}
	}
	if (ys[top] == ys[bot]) {
		return 0;
	}

	u32 ia = top; // vértice actual de la cadena "hacia adelante"
	u32 ib = top; // vértice actual de la cadena "hacia atrás"

	u32 rows = 0;
	for (s32 y = ys[top]; y < ys[bot]; ++y) {
		// Evita aristas horizontales (altura 0) al inicio de cada tramo.
		while (ia != bot && ys[(ia + 1u) % n] == ys[ia]) {
			ia = (ia + 1u) % n;
		}
		while (ib != bot && ys[(ib + n - 1u) % n] == ys[ib]) {
			ib = (ib + n - 1u) % n;
		}
		const u32 na = (ia + 1u) % n;
		const u32 nb = (ib + n - 1u) % n;
		const s32 xa = xs[ia] + (xs[na] - xs[ia]) * (y - ys[ia]) / (ys[na] - ys[ia]);
		const s32 xb = xs[ib] + (xs[nb] - xs[ib]) * (y - ys[ib]) / (ys[nb] - ys[ib]);
		emit(y, xa < xb ? xa : xb, xa < xb ? xb : xa);
		++rows;
		if (na != bot && y + 1 == ys[na]) {
			ia = na;
		}
		if (nb != bot && y + 1 == ys[nb]) {
			ib = nb;
		}
	}
	return rows;
}

} // namespace eng::math3d
