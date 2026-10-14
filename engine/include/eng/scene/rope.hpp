#pragma once

/// \file rope.hpp
/// **Cuerda / puente flexible** (F6 de `ROADMAP_SPRITES_COMPLETO.md` §3.6): cadena de
/// puntos con integración **verlet** y restricciones de distancia (position-based, K
/// pasadas), más el **contacto con un actor** (pies sobre el segmento, apoyo e impulso).
///
/// Genérico sobre el escalar `S` (patrón del repo: `Vec<2,S>`, `scalar_traits`,
/// `scalar_sqrt`, `div_norm`). **Escalar de pantalla: usa `s32` (o `float`)** — las
/// restricciones calculan `dx²+dy²` en `S` y con `s16` desborda a distancias grandes; el
/// emisor convierte a `s16` al dibujar.
///
/// Coste por tick: `O(N)` de integración + `O(K·N)` de restricciones (una división por
/// segmento y pasada, `div_norm`); `N` de 8-16 eslabones sobra para un puente jugable.
/// Congelar la cuerda cuando no hay contacto (el llamador decide si corre `rope_step`).
///
/// Lógica pura (sin hardware, sin heap, sin STL): host-testable.

#include <eng/core/math/linalg.hpp>
#include <eng/core/math/scalar.hpp>
#include <eng/core/math/scalar_math.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::scene {

/// Punto de la cuerda: posición, posición anterior (verlet) y anclaje.
template <typename S>
struct RopePoint {
	eng::math::Vec<2, S> p {};
	eng::math::Vec<2, S> prev {};
	bool pinned = false;
};

/// Cuerda/puente: vista de puntos (del llamador) + parámetros de simulación.
template <typename S>
struct Rope {
	eng::Span<RopePoint<S>> points {};
	S rest_length {};   ///< distancia objetivo entre puntos consecutivos
	S gravity {};       ///< px/tick² (positiva hacia abajo en coordenadas de pantalla)
	u8 iterations = 2;  ///< pasadas de restricción (1-3)
	u16 damping = 256;  ///< 0..256 (256 = sin fricción)
};

/// **Paso completo por tick**: integración verlet + restricciones de distancia.
template <typename S>
inline void rope_step(Rope<S>& rope) noexcept {
	using eng::math::Vec;
	const S zero = eng::math::scalar_traits<S>::zero();
	const S damping = static_cast<S>(rope.damping);

	// 1) Verlet: p += (p - prev) * damping/256 + gravedad.
	for (eng::usize i = 0u; i < rope.points.size(); ++i) {
		RopePoint<S>& q = rope.points[i];
		if (q.pinned) {
			q.prev = q.p;
			continue;
		}
		const Vec<2, S> v = q.p - q.prev;
		q.prev = q.p;
		// `/256` con constante potencia de dos: desplazamiento para enteros, división
		// exacta para float (misma fórmula, un solo camino genérico).
		q.p.v[0] = static_cast<S>(q.p.v[0] + static_cast<S>((v.v[0] * damping) / 256));
		q.p.v[1] = static_cast<S>(q.p.v[1] +
					  static_cast<S>((v.v[1] * damping) / 256) + rope.gravity);
	}

	// 2) Restricciones de distancia (position-based, mitad y mitad).
	for (u8 it = 0u; it < rope.iterations; ++it) {
		for (eng::usize i = 0u; i + 1u < rope.points.size(); ++i) {
			RopePoint<S>& a = rope.points[i];
			RopePoint<S>& b = rope.points[i + 1u];
			const S dx = static_cast<S>(b.p.v[0] - a.p.v[0]);
			const S dy = static_cast<S>(b.p.v[1] - a.p.v[1]);
			const S dist_sq = static_cast<S>(dx * dx + dy * dy);
			if (dist_sq == zero) {
				continue;
			}
			const S dist = eng::math::scalar_sqrt<S>::op(dist_sq);
			if (dist == zero) {
				continue;
			}
			// corrección = delta * (dist - rest) / (2·dist): una división por segmento
			// (sin truncar el factor: el numerador va multiplicado, no el cociente).
			const S diff = static_cast<S>(dist - rope.rest_length);
			const S cx = eng::math::div_norm<S>(static_cast<S>(dx * diff),
							    static_cast<S>(dist * 2));
			const S cy = eng::math::div_norm<S>(static_cast<S>(dy * diff),
							    static_cast<S>(dist * 2));
			if (!a.pinned) {
				a.p.v[0] = static_cast<S>(a.p.v[0] + cx);
				a.p.v[1] = static_cast<S>(a.p.v[1] + cy);
			}
			if (!b.pinned) {
				b.p.v[0] = static_cast<S>(b.p.v[0] - cx);
				b.p.v[1] = static_cast<S>(b.p.v[1] - cy);
			}
		}
	}
}

/// **Impulso verlet** en el punto `index`: desplaza su `prev` en sentido contrario, de modo
/// que la velocidad resultante del siguiente paso es `impulse` (px/tick).
template <typename S>
inline void rope_apply_impulse(Rope<S>& rope, u8 index, eng::math::Vec<2, S> impulse) noexcept {
	if (index >= rope.points.size()) {
		return;
	}
	RopePoint<S>& q = rope.points[index];
	if (q.pinned) {
		return;
	}
	q.prev.v[0] = static_cast<S>(q.prev.v[0] - impulse.v[0]);
	q.prev.v[1] = static_cast<S>(q.prev.v[1] - impulse.v[1]);
}

/// Contacto de los pies de un actor con la cuerda.
template <typename S>
struct RopeContactOf {
	bool hit = false;
	u8 segment = 0;   ///< segmento [i, i+1]
	S surface_y {};   ///< Y de la cuerda en la X de los pies
};

/// **Busca el segmento bajo los pies** (`feet`): proyecta la X sobre cada segmento y toma
/// el más cercano desde arriba (distancia `<= max_snap`). Devuelve `hit = false` si no hay
/// contacto.
template <typename S>
[[nodiscard]] inline RopeContactOf<S> rope_find_contact(const Rope<S>& rope,
							eng::math::Vec<2, S> feet,
							S max_snap) noexcept {
	RopeContactOf<S> c {};
	S best = static_cast<S>(max_snap + eng::math::scalar_traits<S>::one());
	for (eng::usize i = 0u; i + 1u < rope.points.size(); ++i) {
		const RopePoint<S>& a = rope.points[i];
		const RopePoint<S>& b = rope.points[i + 1u];
		const S dx = static_cast<S>(b.p.v[0] - a.p.v[0]);
		const S dy = static_cast<S>(b.p.v[1] - a.p.v[1]);
		const S x_min = dx >= eng::math::scalar_traits<S>::zero() ? a.p.v[0] : b.p.v[0];
		const S x_max = dx >= eng::math::scalar_traits<S>::zero() ? b.p.v[0] : a.p.v[0];
		if (feet.v[0] < x_min || feet.v[0] > x_max) {
			continue; // la X de los pies no cae en el tramo
		}
		S surf = a.p.v[1];
		if (dx != eng::math::scalar_traits<S>::zero()) {
			// y en x=feet: a.y + (feet.x - a.x)·dy/dx (numerador multiplicado, sin
			// truncar el factor).
			surf = static_cast<S>(a.p.v[1] +
					      eng::math::div_norm<S>(
						      static_cast<S>((feet.v[0] - a.p.v[0]) * dy), dx));
		}
		const S d = static_cast<S>(feet.v[1] - surf);
		if (d >= eng::math::scalar_traits<S>::zero() && d < best) {
			best = d;
			c.hit = true;
			c.segment = static_cast<u8>(i);
			c.surface_y = surf;
		}
	}
	return c;
}

/// **Resuelve el contacto**: apoya los pies en la cuerda y, si el actor caía (`vy > 0`),
/// reparte el impulso entre los dos puntos del segmento y rebota `vy` según `bounce`
/// (0 = pegajoso; 256 = rebote total). Devuelve `vy`/`feet` actualizados por referencia.
template <typename S>
inline void rope_resolve_player(Rope<S>& rope, eng::math::Vec<2, S>& feet, S& vy,
				const RopeContactOf<S>& c, u16 bounce = 0u) noexcept {
	if (!c.hit) {
		return;
	}
	feet.v[1] = c.surface_y;
	if (vy > eng::math::scalar_traits<S>::zero()) {
		const S half = eng::math::div_norm<S>(vy, eng::math::scalar_traits<S>::from_int(2));
		eng::math::Vec<2, S> imp {};
		imp.v[1] = half;
		rope_apply_impulse(rope, c.segment, imp);
		imp.v[1] = static_cast<S>(vy - half);
		rope_apply_impulse(rope, static_cast<u8>(c.segment + 1u), imp);
		// rebote: vy = -vy·bounce/256
		vy = static_cast<S>(-static_cast<S>((vy * static_cast<S>(bounce)) / 256));
	} else {
		vy = eng::math::scalar_traits<S>::zero();
	}
}

} // namespace eng::scene
