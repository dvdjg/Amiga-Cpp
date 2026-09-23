#pragma once

/// \file polygon.hpp
/// **Generación de spans de un polígono convexo** en pantalla: el polígono se recorre por
/// **dos cadenas** (izquierda/derecha) desde el vértice superior al inferior, y por cada
/// scanline se emite `(y, xl, xr)`. Es O(altura) frente a O(lados·altura) del barrido que
/// recalcula el mínimo/máximo sobre todas las aristas en cada fila. Es la forma amiga del
/// raster Amiga (rellena polígonos convexos): el llamador escribe cada span como quiera
/// (píxel a píxel, un `write_span`, o un blit).
///
/// ```text
///   polígono convexo (vértices en orden de giro)           por cada scanline y:
///   ────────────────────────────────────────────           emit(y, xl, xr)
///            top (vértice superior)                            ▲
///            /  \                 cadena A ──► (avanza)  ──────┤ xl
///           /    \                cadena B ──◄ (retrocede) ────┘ xr
///          /      \                     │
///         /        \                    ▼
///        bottom (vértice inferior)  O(altura)  vs  O(lados · altura) del barrido ingenuo
///   La fila inferior queda semiaabierta (igual que el barrido de referencia).
/// ```

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::math3d {

/// Genera los **spans** horizontales `(y, xl, xr)` de un polígono **convexo** por dos
/// cadenas. `emit(y, xl, xr)` se llama una vez por scanline (con `xl <= xr`; la fila
/// inferior queda semiaabierta, como el barrido de referencia). Los vértices deben estar en
/// orden de giro y formar un polígono convexo.
///
/// La interpolación de cada arista es un **DDA incremental** (acumulador de error), **sin
/// división** por scanline (nada de `__divsi3` en el relleno de polígono).
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

	/// Estado DDA de una cadena: interpola `x` a lo largo de la arista `a->b` fila a fila.
	struct Dda {
		s32 x = 0;
		s32 adx = 0; // |dx|
		s32 dy = 0;  // altura (aristas horizontales se saltan antes)
		s32 rem = 0;
		s32 sx = 1;
	};
	auto setup = [&](Dda& d, u32 a, u32 b) {
		d.dy = ys[b] - ys[a];
		const s32 dx = xs[b] - xs[a];
		d.sx = dx >= 0 ? 1 : -1;
		d.adx = dx >= 0 ? dx : -dx;
		d.x = xs[a];
		// Truncamiento (half-open), no redondeo: el DDA debe reproducir EXACTAMENTE el
		// barrido `x0 + (x1-x0)*(y-y0)/(y1-y0)` de la referencia (división entera).
		d.rem = 0;
	};
	auto step = [&](Dda& d) {
		d.rem += d.adx;
		if (d.rem >= d.dy) {
			d.rem -= d.dy;
			d.x += d.sx;
		}
	};

	u32 ia = top; // vértice actual de la cadena "hacia adelante"
	u32 ib = top; // vértice actual de la cadena "hacia atrás"
	// Salta aristas horizontales (altura 0) al inicio de cada cadena.
	while (ia != bot && ys[(ia + 1u) % n] == ys[ia]) {
		ia = (ia + 1u) % n;
	}
	while (ib != bot && ys[(ib + n - 1u) % n] == ys[ib]) {
		ib = (ib + n - 1u) % n;
	}
	u32 na = (ia + 1u) % n;
	u32 nb = (ib + n - 1u) % n;
	Dda da {};
	Dda db {};
	setup(da, ia, na);
	setup(db, ib, nb);

	u32 rows = 0;
	for (s32 y = ys[top]; y < ys[bot]; ++y) {
		while (na != bot && y >= ys[na]) {
			ia = na;
			na = (ia + 1u) % n;
			while (na != bot && ys[na] == ys[ia]) {
				ia = na;
				na = (ia + 1u) % n;
			}
			setup(da, ia, na);
		}
		while (nb != bot && y >= ys[nb]) {
			ib = nb;
			nb = (ib + n - 1u) % n;
			while (nb != bot && ys[nb] == ys[ib]) {
				ib = nb;
				nb = (ib + n - 1u) % n;
			}
			setup(db, ib, nb);
		}
		emit(y, da.x < db.x ? da.x : db.x, da.x < db.x ? db.x : da.x);
		++rows;
		step(da);
		step(db);
	}
	return rows;
}

} // namespace eng::math3d
