#pragma once

/// \file walkbox.hpp
/// **Walkbox de aventura** (`eng::adv`): región convexa de suelo por la que un actor puede
/// caminar, con la escala del actor y los flags del original. Es **contenido** (datos de la
/// sala), no motor: el pathfinding del runtime (A0 de `ROADMAP_ADVENTURE.md`) construye su
/// grafo sobre estas cajas; aquí viven solo las consultas puras y genéricas sobre el escalar
/// `S` (s16/s32/float), sin hardware y sin estado.
///
/// Geometría como en el original SCUMM V5 (`engines/scumm/boxes.cpp`, struct `Box`): cuatro
/// esquinas en orden ul→ur→lr→ll (cuadrilátero convexo) y `scale` (255 = 100 %) que fija el
/// tamaño del actor dentro de la caja. El contenido convertido llega en el orden canónico:
/// `SIM v2 (walkboxes)` → tablas generadas por `tools/adventure/codegen/emit-content.mjs`.
///
/// La aritmética usa la **promoción natural** del escalar (`s16` × `s16` → `int` en C++), de
/// modo que las coordenadas de SCUMM (≤ 320×200) no desbordan; con `s32` los productos deben
/// caber en `s32`.

#include <eng/core/math/linalg.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/adv/types.hpp>

namespace eng::adv {

/// Producto cruz `(b−a)×(p−a)` con promoción natural del escalar; su **signo** clasifica a
/// `p` respecto de la arista `a→b` (convenio antihorario positivo).
/// \param a primer extremo de la arista.
/// \param b segundo extremo de la arista.
/// \param p punto a clasificar.
/// \return el producto cruz en el tipo promovido de `S`.
template <class S>
[[nodiscard]] constexpr decltype(S {} * S {}) cross_rel(eng::math::Vec<2, S> a, eng::math::Vec<2, S> b,
							eng::math::Vec<2, S> p) noexcept {
	using P = decltype(S {} * S {});
	return (static_cast<P>(b.v[0]) - a.v[0]) * (static_cast<P>(p.v[1]) - a.v[1]) -
	       (static_cast<P>(b.v[1]) - a.v[1]) * (static_cast<P>(p.v[0]) - a.v[0]);
}

/// Tipo promovido para las **distancias al cuadrado** de una walkbox: la distancia puede
/// exceder el rango de `S` (una sala de 320×200 da hasta ~150 000 en s16), así que se
/// devuelve en el tipo de la promoción natural (`s16` → `int`).
template <class S>
using walkbox_dist_t = decltype(S {} * S {});

/// Punto del segmento `a→b` más cercano a `p`, con su distancia al cuadrado.
/// \param a primer extremo del segmento.
/// \param b segundo extremo del segmento.
/// \param p punto de consulta.
/// \param dist2 salida: distancia al cuadrado (tipo promovido `walkbox_dist_t<S>`) del punto
/// devuelto a `p`.
/// \return punto del segmento (extremos incluidos) más cercano a `p`.
/// \note La división de la proyección interior corre solo al fijar un destino de caminata
/// (petición puntual), nunca en bucle por frame.
template <class S>
[[nodiscard]] constexpr eng::math::Vec<2, S> closest_on_segment(eng::math::Vec<2, S> a,
								eng::math::Vec<2, S> b,
								eng::math::Vec<2, S> p,
								walkbox_dist_t<S>& dist2) noexcept {
	using P = decltype(S {} * S {});
	const P dx = static_cast<P>(b.v[0]) - a.v[0];
	const P dy = static_cast<P>(b.v[1]) - a.v[1];
	const P px = static_cast<P>(p.v[0]) - a.v[0];
	const P py = static_cast<P>(p.v[1]) - a.v[1];
	const P len2 = dx * dx + dy * dy;
	P qx = a.v[0];
	P qy = a.v[1];
	if (len2 > P {0}) {
		P t = px * dx + py * dy; // proyección escalada por len2
		if (t < P {0}) {
			t = P {0};
		} else if (t > len2) {
			t = len2;
		}
		qx += (t * dx) / len2;
		qy += (t * dy) / len2;
	}
	const P ex = static_cast<P>(p.v[0]) - qx;
	const P ey = static_cast<P>(p.v[1]) - qy;
	dist2 = ex * ex + ey * ey;
	return eng::math::Vec<2, S> {{static_cast<S>(qx), static_cast<S>(qy)}};
}

/// Walkbox: cuadrilátero convexo de suelo + datos de la caja original.
/// \tparam S escalar de coordenada (s16/s32/float).
template <class S>
struct Walkbox {
	eng::math::Vec<2, S> ul {}, ur {}, lr {}, ll {}; ///< esquinas en orden ul→ur→lr→ll
	eng::u8 mask {0};                                ///< máscara de la caja original (SCUMM)
	eng::u8 flags {0};                               ///< flags de caja (`walkbox_flags`)
	eng::u8 scale {255};                             ///< escala del actor dentro (255 = 100 %)

	/// ¿Caja utilizable? Descarta el centinela del original: las cuatro esquinas exactamente
	/// en (−32000, −32000), como los huecos de caja de las salas SCUMM.
	/// \return `true` si la caja se puede usar para caminar/consultas.
	[[nodiscard]] constexpr bool valid() const noexcept {
		const eng::math::Vec<2, S> sentinel {{static_cast<S>(-32000), static_cast<S>(-32000)}};
		return !(ul.v[0] == sentinel.v[0] && ul.v[1] == sentinel.v[1] && ur.v[0] == sentinel.v[0] &&
			 ur.v[1] == sentinel.v[1] && lr.v[0] == sentinel.v[0] && lr.v[1] == sentinel.v[1] &&
			 ll.v[0] == sentinel.v[0] && ll.v[1] == sentinel.v[1]);
	}

	/// ¿`p` está dentro del cuadrilátero convexo? (cuatro productos cruz del mismo signo;
	/// el borde cuenta como dentro, como en el original.)
	/// \param p punto en coordenadas de la sala.
	/// \return `true` si `p` cae dentro (o en el borde).
	[[nodiscard]] constexpr bool contains(eng::math::Vec<2, S> p) const noexcept {
		const auto c1 = cross_rel(ul, ur, p);
		const auto c2 = cross_rel(ur, lr, p);
		const auto c3 = cross_rel(lr, ll, p);
		const auto c4 = cross_rel(ll, ul, p);
		const bool non_neg = c1 >= 0 && c2 >= 0 && c3 >= 0 && c4 >= 0;
		const bool non_pos = c1 <= 0 && c2 <= 0 && c3 <= 0 && c4 <= 0;
		return non_neg || non_pos;
	}

	/// Punto del **borde** de la caja más cercano a `p` (equivalente a `getClosestPtOnBox`
	/// del original): base del "walk to" cuando el destino cae fuera de la caja.
	/// \param p destino pedido, en coordenadas de la sala.
	/// \param dist2 salida: distancia al cuadrado (tipo promovido `walkbox_dist_t<S>`).
	/// \return punto de borde más cercano.
	[[nodiscard]] constexpr eng::math::Vec<2, S> closest_point(eng::math::Vec<2, S> p,
								  walkbox_dist_t<S>& dist2) const noexcept {
		using P = decltype(S {} * S {});
		P best {};
		eng::math::Vec<2, S> best_pt = ul;
		const eng::math::Vec<2, S> corners[4] {ul, ur, lr, ll};
		for (int i = 0; i < 4; i++) {
			walkbox_dist_t<S> d {};
			const eng::math::Vec<2, S> q = closest_on_segment(corners[i], corners[(i + 1) & 3], p, d);
			if (i == 0 || d < best) {
				best = d;
				best_pt = q;
			}
		}
		dist2 = best;
		return best_pt;
	}
};

/// Índice de la **última** caja válida que contiene `p` (la del frente: mismo orden que el
/// hit-test del original, que recorre de la última a la primera), o `kWalkboxInvalid`.
/// \param boxes vista de cajas de la sala (orden de datos: frente al final).
/// \param p punto en coordenadas de la sala.
/// \return índice de la caja o `kWalkboxInvalid`.
template <class S>
[[nodiscard]] constexpr eng::u16 box_at(eng::Span<const Walkbox<S>> boxes, eng::math::Vec<2, S> p) noexcept {
	for (eng::usize i = boxes.size(); i > 0; --i) {
		const Walkbox<S>& box = boxes[i - 1];
		if (box.valid() && box.contains(p)) {
			return static_cast<eng::u16>(i - 1);
		}
	}
	return kWalkboxInvalid;
}

} // namespace eng::adv
