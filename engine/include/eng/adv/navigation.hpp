#pragma once

/// \file navigation.hpp
/// **Navegación por walkboxes** (`eng::adv`, hito A0): ruta sobre el grafo geométrico de cajas
/// y suavizado por visibilidad, sin heap y genérico sobre el escalar `S`. La adyacencia se
/// **deriva de la geometría** (cajas que comparten borde o se tocan dentro de una tolerancia),
/// de modo que el contenido del engine no necesita la matriz BOXM del original (que queda en
/// el SIM para referencia/diff).
///
/// Modelo: las cajas son convexas (ver `walkbox.hpp`). El router hace Dijkstra sobre cajas
/// (n ≤ decenas; selección lineal, sin cola dinámica), reconstruye la cadena y genera puntos
/// de paso en los bordes compartidos; después elimina waypoints intermedios cuando el segmento
/// entre dos puntos permanece dentro de la unión de cajas (muestreo de visibilidad).
///
/// Coste: petición puntual al fijar un destino de caminata (no es bucle por frame); la
/// distancia entre cajas usa la promoción del escalar (`walkbox_dist_t`).

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/adv/types.hpp>
#include <eng/adv/walkbox.hpp>

namespace eng::adv {

/// Buffer de trabajo del router (una entrada por caja; lo aporta el llamador, sin heap).
/// \tparam S escalar de coordenada.
template <class S>
struct RouteScratch {
	eng::Span<walkbox_dist_t<S>> best {}; ///< coste acumulado (distancia²); el router lo inicializa
	eng::Span<eng::u16> prev {};          ///< caja predecesora en la ruta
	eng::Span<eng::u8> state {};          ///< 0 = pendiente, 1 = cerrada
};

/// ¿Se intersectan los segmentos `a0a1` y `b0b1` (incluye tocar/extremos)?
/// \return `true` si hay intersección; se usa para derivar adyacencia de cajas.
template <class S>
[[nodiscard]] constexpr bool segments_intersect(eng::math::Vec<2, S> a0, eng::math::Vec<2, S> a1,
						 eng::math::Vec<2, S> b0,
						 eng::math::Vec<2, S> b1) noexcept {
	const auto o1 = cross_rel(a0, a1, b0);
	const auto o2 = cross_rel(a0, a1, b1);
	const auto o3 = cross_rel(b0, b1, a0);
	const auto o4 = cross_rel(b0, b1, a1);
	const bool straddle_a = (o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0);
	const bool straddle_b = (o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0);
	if (straddle_a && straddle_b) return true;
	// Contacto/colinealidad: si un extremo toca el otro segmento por caja envolvente.
	const auto in_range = [](S x, S lo, S hi) noexcept { return x >= lo && x <= hi; };
	const auto on_seg = [&](eng::math::Vec<2, S> p, eng::math::Vec<2, S> s0,
				eng::math::Vec<2, S> s1) noexcept {
		return cross_rel(s0, s1, p) == 0 &&
		       in_range(p.v[0], s0.v[0] < s1.v[0] ? s0.v[0] : s1.v[0], s0.v[0] < s1.v[0] ? s1.v[0] : s0.v[0]) &&
		       in_range(p.v[1], s0.v[1] < s1.v[1] ? s0.v[1] : s1.v[1], s0.v[1] < s1.v[1] ? s1.v[1] : s0.v[1]);
	};
	return on_seg(b0, a0, a1) || on_seg(b1, a0, a1) || on_seg(a0, b0, b1) || on_seg(a1, b0, b1);
}

/// ¿Son adyacentes dos cajas? Contacto/traslape dentro de una tolerancia (en píxeles): un
/// vértice de una dentro de la otra, o bordes que se cortan/tocan, o distancia entre bordes
/// ≤ `tolerance`.
/// \param a primera caja (válida).
/// \param b segunda caja (válida).
/// \param tolerance tolerancia en píxeles (p. ej. 2 para mapas con filtraciones de 1 px).
/// \return `true` si se puede pasar de una a otra.
template <class S>
[[nodiscard]] constexpr bool boxes_adjacent(const Walkbox<S>& a, const Walkbox<S>& b,
					    S tolerance) noexcept {
	if (!a.valid() || !b.valid()) return false;
	const eng::math::Vec<2, S> ca[4] {a.ul, a.ur, a.lr, a.ll};
	const eng::math::Vec<2, S> cb[4] {b.ul, b.ur, b.lr, b.ll};
	for (int i = 0; i < 4; i++) {
		if (b.contains(ca[i]) || a.contains(cb[i])) return true;
		for (int j = 0; j < 4; j++) {
			if (segments_intersect(ca[i], ca[(i + 1) & 3], cb[j], cb[(j + 1) & 3])) return true;
		}
	}
	// Distancia entre bordes dentro de tolerancia: mínimo de closest_point de los vértices.
	const walkbox_dist_t<S> tol2 = static_cast<walkbox_dist_t<S>>(tolerance) *
				       static_cast<walkbox_dist_t<S>>(tolerance);
	for (int i = 0; i < 4; i++) {
		walkbox_dist_t<S> d {};
		(void)a.closest_point(cb[i], d);
		if (d <= tol2) return true;
	}
	return false;
}

/// Distancia (al cuadrado) entre los bordes de dos cajas, aproximada por los vértices.
/// \return distancia² mínima entre un vértice de `b` y el borde de `a`.
template <class S>
[[nodiscard]] constexpr walkbox_dist_t<S> boxes_gap2(const Walkbox<S>& a,
						      const Walkbox<S>& b) noexcept {
	const eng::math::Vec<2, S> cb[4] {b.ul, b.ur, b.lr, b.ll};
	walkbox_dist_t<S> best = static_cast<walkbox_dist_t<S>>(-1);
	for (int i = 0; i < 4; i++) {
		walkbox_dist_t<S> d {};
		(void)a.closest_point(cb[i], d);
		if (best < 0 || d < best) best = d;
	}
	return best;
}

/// ¿El segmento `p→q` permanece dentro de la unión de cajas? (muestreo; referencia de
/// visibilidad para el suavizado de la ruta).
/// \param boxes cajas de la sala.
/// \param p origen del segmento.
/// \param q destino del segmento.
/// \param samples número de puntos de control (≥ 2).
/// \return `true` si todos los puntos de control caen en alguna caja válida.
template <class S>
[[nodiscard]] constexpr bool line_clear(eng::Span<const Walkbox<S>> boxes, eng::math::Vec<2, S> p,
					eng::math::Vec<2, S> q, eng::u16 samples = 8) noexcept {
	const eng::u16 n = samples < 2 ? 2 : samples;
	for (eng::u16 i = 0; i <= n; i++) {
		const S t = static_cast<S>(i) / static_cast<S>(n);
		const eng::math::Vec<2, S> pt {{static_cast<S>(p.v[0] + (q.v[0] - p.v[0]) * t),
					static_cast<S>(p.v[1] + (q.v[1] - p.v[1]) * t)}};
		if (box_at(boxes, pt) == kWalkboxInvalid) return false;
	}
	return true;
}

/// Calcula una ruta caminable entre dos puntos del mundo a través de las walkboxes.
/// \param boxes cajas de la sala (orden de datos; `valid()` filtra centinelas).
/// \param from punto de salida (mundo).
/// \param to destino pedido (mundo; si cae fuera, se ajusta al borde de la última caja).
/// \param adjacency_tolerance tolerancia de contacto entre cajas (px).
/// \param scratch buffers de trabajo con capacidad ≥ `boxes.size()`.
/// \param out puntos de salida (≥ 2 recomendado); se devuelven **en orden de avance**.
/// \return número de puntos escritos; 0 si no hay ruta (p. ej. cajas desconectadas).
template <class S>
[[nodiscard]] eng::usize walkbox_route(eng::Span<const Walkbox<S>> boxes, eng::math::Vec<2, S> from,
				       eng::math::Vec<2, S> to, S adjacency_tolerance,
				       RouteScratch<S> scratch, eng::Span<eng::math::Vec<2, S>> out) noexcept {
	const eng::usize n = boxes.size();
	if (n == 0 || scratch.best.size() < n || scratch.prev.size() < n || scratch.state.size() < n ||
	    out.size() < 2) {
		return 0;
	}
	const eng::u16 start = box_at(boxes, from);
	const eng::u16 goal = box_at(boxes, to);
	if (start == kWalkboxInvalid || goal == kWalkboxInvalid) return 0;

	if (start == goal) {
		// Misma caja (convexa): directo si el destino está dentro; si no, borde más cercano.
		walkbox_dist_t<S> d {};
		const eng::math::Vec<2, S> end = boxes[start].contains(to) ? to : boxes[start].closest_point(to, d);
		out[0] = end;
		return 1;
	}

	// Dijkstra sobre cajas (selección lineal; n pequeño).
	for (eng::usize i = 0; i < n; i++) {
		scratch.best[i] = static_cast<walkbox_dist_t<S>>(-1);
		scratch.prev[i] = kWalkboxInvalid;
		scratch.state[i] = 0;
	}
	scratch.best[start] = 0;
	for (eng::usize iter = 0; iter < n; iter++) {
		eng::u16 u = kWalkboxInvalid;
		walkbox_dist_t<S> best = static_cast<walkbox_dist_t<S>>(-1);
		for (eng::usize i = 0; i < n; i++) {
			if (scratch.state[i] == 0 && scratch.best[i] >= 0 && (best < 0 || scratch.best[i] < best)) {
				best = scratch.best[i];
				u = static_cast<eng::u16>(i);
			}
		}
		if (u == kWalkboxInvalid || u == goal) break;
		scratch.state[u] = 1;
		for (eng::usize v = 0; v < n; v++) {
			if (scratch.state[v] != 0 || v == u) continue;
			if (!boxes_adjacent(boxes[u], boxes[v], adjacency_tolerance)) continue;
			const walkbox_dist_t<S> step = boxes_gap2(boxes[u], boxes[v]);
			const walkbox_dist_t<S> cand = scratch.best[u] + step;
			if (scratch.best[v] < 0 || cand < scratch.best[v]) {
				scratch.best[v] = cand;
				scratch.prev[v] = u;
			}
		}
	}
	if (scratch.prev[goal] == kWalkboxInvalid) return 0;

	// Cadena de cajas start→goal y puntos de paso en los bordes de entrada.
	eng::u16 chain[64] {};
	eng::usize chain_len = 0;
	for (eng::u16 c = goal; c != kWalkboxInvalid && chain_len < 64; c = scratch.prev[c]) {
		chain[chain_len++] = c;
		if (c == start) break;
	}
	if (chain_len == 0 || chain[chain_len - 1] != start) return 0;

	eng::usize count = 0;
	eng::math::Vec<2, S> cursor = from;
	for (eng::usize k = chain_len - 1; k > 0; k--) {
		const eng::u16 current = chain[k];
		const eng::u16 next = chain[k - 1];
		// Punto de entrada a la caja siguiente desde el cursor actual.
		walkbox_dist_t<S> d {};
		const eng::math::Vec<2, S> wp = boxes[next].closest_point(cursor, d);
		if (count < out.size()) out[count++] = wp;
		// Avanza el cursor al borde de la caja actual más cercano al siguiente punto.
		const eng::math::Vec<2, S> leave = boxes[current].closest_point(wp, d);
		cursor = leave;
	}
	// Punto final: destino si cae dentro de la caja meta; si no, su borde más cercano.
	walkbox_dist_t<S> d {};
	const eng::math::Vec<2, S> end = boxes[goal].contains(to) ? to : boxes[goal].closest_point(to, d);
	if (count < out.size()) out[count++] = end;

	// Suavizado: elimina waypoints intermedios si el segmento es visible (dentro de la unión).
	if (count > 2) {
		eng::usize kept = 1; // se conserva el primero
		for (eng::usize j = 2; j < count; j++) {
			if (line_clear(boxes, out[kept - 1], out[j])) continue; // se puede saltar out[j-1]
			out[kept++] = out[j - 1];
		}
		out[kept++] = out[count - 1];
		count = kept;
	}
	return count;
}

} // namespace eng::adv
