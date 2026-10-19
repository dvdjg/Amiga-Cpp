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
#include <eng/core/util/type_traits.hpp>

namespace eng::math3d {

/// Genera los **spans** horizontales `(y, xl, xr)` de un polígono **convexo** por dos
/// cadenas. `emit(y, xl, xr)` se llama una vez por scanline (con `xl <= xr`; la fila
/// inferior queda semiaabierta, como el barrido de referencia). Los vértices deben estar en
/// orden de giro y formar un polígono convexo. El tipo de coordenada es parámetro
/// (`T`, entero con signo): sirve pantalla `s16`, coordenadas `s32` o cualquier ancho.
///
/// La interpolación de cada arista es un **DDA incremental** (acumulador de error), **sin
/// división** por scanline (nada de `__divsi3` en el relleno de polígono).
template <class T, class Emit>
inline u32 convex_spans(Span<const T> xs, Span<const T> ys, Emit&& emit) {
	static_assert(eng::util::is_integral_v<T> && eng::util::is_signed_v<T>,
		      "convex_spans: T debe ser un entero con signo (coordenadas)");
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
		T x = 0;
		T adx = 0; // |dx|
		T dy = 0;  // altura (aristas horizontales se saltan antes)
		T rem = 0;
		T sx = 1;
	};
	auto setup = [&](Dda& d, u32 a, u32 b) {
		d.dy = static_cast<T>(ys[b] - ys[a]);
		const T dx = static_cast<T>(xs[b] - xs[a]);
		d.sx = dx >= 0 ? T {1} : T {-1};
		d.adx = dx >= 0 ? dx : static_cast<T>(-dx);
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

	// Envoltura incremental: `% n` en un bucle por fila acaba en `__umodsi3` (~150
	// ciclos) en 68000. Aquí n es runtime, así que se sustituye por comparación.
	auto next = [n](u32 i) { return (i + 1u == n) ? 0u : (i + 1u); };
	auto prev = [n](u32 i) { return (i == 0u) ? (n - 1u) : (i - 1u); };
	u32 ia = top; // vértice actual de la cadena "hacia adelante"
	u32 ib = top; // vértice actual de la cadena "hacia atrás"
	// Salta aristas horizontales (altura 0) al inicio de cada cadena.
	while (ia != bot && ys[next(ia)] == ys[ia]) {
		ia = next(ia);
	}
	while (ib != bot && ys[prev(ib)] == ys[ib]) {
		ib = prev(ib);
	}
	u32 na = next(ia);
	u32 nb = prev(ib);
	Dda da {};
	Dda db {};
	setup(da, ia, na);
	setup(db, ib, nb);

	u32 rows = 0;
	for (T y = ys[top]; y < ys[bot]; ++y) {
		while (na != bot && y >= ys[na]) {
			ia = na;
			na = next(ia);
			while (na != bot && ys[na] == ys[ia]) {
				ia = na;
				na = next(ia);
			}
			setup(da, ia, na);
		}
		while (nb != bot && y >= ys[nb]) {
			ib = nb;
			nb = prev(ib);
			while (nb != bot && ys[nb] == ys[ib]) {
				ib = nb;
				nb = prev(ib);
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
