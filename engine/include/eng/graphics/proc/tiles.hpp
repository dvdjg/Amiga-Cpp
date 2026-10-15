#pragma once

/// \file tiles.hpp
/// **Tiles Wang/Carcassonne**: cada tile codifica un identificador de borde por lado
/// (N/E/S/W) y dos tiles conectan si el borde compartido tiene el mismo id. Es el
/// pegamento del terreno modular (`terrain.hpp`) y la base de las variantes
/// rotadas/reflejadas y del borde suave (rampa + dither).
///
/// ```text
///   TileEdges (N,E,S,W)            dos tiles casan por el lado compartido:
///      ┌───n───┐                     a.n == b.s   (a arriba de b)
///      │       │                     a.e == b.w   (a a la izquierda de b)
///    w │ tile  │ e                   …
///      │       │
///      └───s───┘
///
///   variantes: apply_transform (píxeles) + transform_edges (ids) → girar/reflejar un
///   tile base conserva sus conexiones (8 variantes por 1 base, memoria ÷8)
/// ```
///
/// El tipo del id de borde es un **parámetro** (`EdgeSides<E>`): el terreno usa su enum
/// de rasgos (`Edge`), y un uso genérico puede usar `eng::u8`. El pegamento no conoce
/// el significado del id, solo la igualdad.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); sin demo que la ejercite en hardware.

#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/dither.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>

namespace eng::gfx::proc {

/// Lado de un tile para el emparejamiento de bordes (el orden es el del giro horario).
enum class Side : eng::u8 { North, East, South, West };

/// Conjunto de los cuatro bordes de un tile, parametrizado en el tipo del identificador
/// (`eng::u8` genérico o `terrain::Edge`). El valor por defecto es el id 0 del tipo.
/// \tparam E tipo del identificador de borde (comparable con `==`).
template <class E>
struct EdgeSides {
	/// Borde superior (el que casa con el borde `s` del tile de arriba).
	E n {};
	/// Borde derecho (el que casa con el borde `w` del tile de la derecha).
	E e {};
	/// Borde inferior (el que casa con el borde `n` del tile de abajo).
	E s {};
	/// Borde izquierdo (el que casa con el borde `e` del tile de la izquierda).
	E w {};
};

/// ¿Los bordes de `a` y `b` casan por el lado `s` (`a.n == b.s`, etc.)? La comprobación
/// es la misma relación que usa el colocador de mapas y `can_place` del terreno.
/// \tparam E tipo del id de borde. \param a tile de referencia. \param b vecino.
/// \param s lado de `a` por el que toca `b`. \return true si la unión es válida.
template <class E>
[[nodiscard]] constexpr bool edges_match(EdgeSides<E> a, EdgeSides<E> b, Side s) {
	switch (s) {
	case Side::North: return a.n == b.s;
	case Side::East: return a.e == b.w;
	case Side::South: return a.s == b.n;
	case Side::West: return a.w == b.e;
	}
	return false; // inalcanzable (switch exhaustivo); silencia el aviso de retorno
}

/// Transformaciones geométricas de un tile cuadrado (las 6 variantes baratas; las demás
/// se componen): rotaciones de 90° horarias y reflejos de eje. Generar 1 tile y derivar
/// sus variantes multiplica la variedad sin recalcular ruido (memoria y CPU ÷6).
enum class Transform : eng::u8 { Identity, Rot90, Rot180, Rot270, FlipX, FlipY };

/// Aplica la transformación de `t` a los **bordes** (dónde cae cada id tras girar el
/// tile). Los giros están verificados como el inverso del mapeo de píxel de
/// `apply_transform` (misma convención de ejes: `x` a la derecha, `y` hacia abajo).
/// \tparam E tipo del id de borde. \param e bordes del tile base. \param t transformación.
/// \return bordes del tile transformado.
template <class E>
[[nodiscard]] constexpr EdgeSides<E> transform_edges(EdgeSides<E> e, Transform t) {
	switch (t) {
	case Transform::Rot90: return {e.w, e.n, e.e, e.s};
	case Transform::Rot180: return {e.s, e.w, e.n, e.e};
	case Transform::Rot270: return {e.e, e.s, e.w, e.n};
	case Transform::FlipX: return {e.n, e.w, e.s, e.e};
	case Transform::FlipY: return {e.s, e.e, e.n, e.w};
	default: return e;
	}
}

/// Copia `src` en `dst` con la transformación `t` (buffer cuadrado de lado `N`).
/// El píxel `dst(x, y)` se toma de `src(sx, sy)` según la variante; los ids de borde se
/// transforman con `transform_edges` (el llamador recibe ambos lados coherentes).
/// \tparam N lado del buffer (cuadrado). \param src tile base. \param dst buffer destino.
/// \param t transformación a aplicar.
template <eng::u16 N>
constexpr void apply_transform(const BitmapBuffer<N, N>& src, BitmapBuffer<N, N>& dst, Transform t) {
	for (eng::u16 y = 0; y < N; ++y) {
		for (eng::u16 x = 0; x < N; ++x) {
			eng::u16 sx = x;
			eng::u16 sy = y;
			switch (t) {
			case Transform::Rot90: sx = y; sy = N - 1u - x; break;
			case Transform::Rot180: sx = N - 1u - x; sy = N - 1u - y; break;
			case Transform::Rot270: sx = N - 1u - y; sy = x; break;
			case Transform::FlipX: sx = N - 1u - x; break;
			case Transform::FlipY: sy = N - 1u - y; break;
			default: break;
			}
			dst.at(x, y) = src.at(sx, sy);
		}
	}
}

/// **Rampa de borde** de `W` pasos: `smoothstep` discreto 0..255 (0 en el interior del
/// tile, 255 en el borde exterior). Se precalcula en compilación; el pintor de bordes
/// solo indexa (`t[i]`). El clamp final evita el `3·255²−2·255³` en aritmética de 8 bits
/// del doc original, que desbordaba y daba 2 en el último paso.
/// \tparam W ancho de la franja de borde en píxeles (4, 6 u 8 típicos).
template <eng::u8 W>
struct BorderRamp {
	/// Peso 0..255 de cada paso (0 = solo interior, 255 = solo borde).
	eng::u8 t[W] {};

	/// Construye la rampa `smoothstep` cuantizada a 8 bits.
	constexpr BorderRamp() {
		for (eng::u8 i = 0; i < W; ++i) {
			const eng::u32 u = (W == 1u) ? 255u : (eng::u32(i) * 255u) / (W - 1u);
			const eng::u32 u2 = (u * u) >> 8u;
			const eng::u32 u3 = (u2 * u) >> 8u;
			const eng::u32 v = 3u * u2 - 2u * u3; // smoothstep 3u²−2u³ en escala 256
			t[i] = eng::u8(v > 255u ? 255u : v);
		}
	}
};

/// Rampas listas para las franjas de borde de 4, 6 y 8 píxeles.
inline constexpr BorderRamp<4> kRamp4 {};
/// \copydoc kRamp4
inline constexpr BorderRamp<6> kRamp6 {};
/// \copydoc kRamp4
inline constexpr BorderRamp<8> kRamp8 {};

/// Mezcla dos **índices** de paleta con dither espacial Bayer: `alpha` (0..255) es el
/// peso del borde. No interpola color (no hay RGB en este nivel): el píxel elige uno u
/// otro según el umbral de su celda, y el promedio se percibe a distancia (técnica
/// clásica de la Amiga con paletas indexadas). El test es `>` (no `>=`) para que
/// `alpha == 0` deje **siempre** el interior aunque la celda tenga umbral 0.
/// \param interior índice de la capa de fondo. \param border índice de la capa de borde.
/// \param alpha peso del borde 0..255. \param x,y píxel (módulo 8 en el umbral).
/// \return `border` con probabilidad espacial ~`alpha/255`, si no `interior`.
[[nodiscard]] constexpr eng::u8 blend_index(eng::u8 interior, eng::u8 border, eng::u8 alpha,
					    eng::u16 x, eng::u16 y) {
	return (alpha > Bayer::threshold8(x, y)) ? border : interior;
}

} // namespace eng::gfx::proc
