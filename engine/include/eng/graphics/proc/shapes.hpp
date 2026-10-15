#pragma once

/// \file shapes.hpp
/// **Primitivas orgánicas de dibujo procedural** (`eng::gfx::proc::shapes`): evaluadores de
/// píxel que devuelven un índice de color (0 = fondo/transparente) para ríos, lagos,
/// copas de árbol, rocas, cultivos, murallas, torres, casas, cuevas y máscaras suaves.
/// Son los "sellos" con los que el pintor de terreno (`terrain.hpp`) compone cada tile.
///
/// Todas evitan el `sqrt` (comparan distancias al cuadrado) y las divisiones por píxel
/// salvo donde se documenta y se acota el área afectada; el álgebra está reescrita para
/// que la comparación sea una **multiplicación** (p. ej. la elipse
/// `(dx²/rx² + dy²/ry²) < 1` se convierte en `dx²·ry² + dy²·rx² < rx²·ry²`, idéntica pero
/// sin división). El coste es de **generación** (carga); nunca se evalúan por frame.
///
/// Las que usan ruido son plantillas sobre el escalar `S` (§1.11): en el Amiga el válido
/// es `MiniFloat16` (`noise.hpp`); en host, `float`. Las puramente geométricas (muralla,
/// torre, casa, cueva) valen para cualquier escalar del consumidor.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); la demo 119 usa las piezas ya pintadas
/// (precocinadas en host), así que estos sellos no corren en hardware — motivo y medida en
/// `PROCEDURAL_BITMAPS.md` §Coste.

#include <eng/core/math/noise.hpp>
#include <eng/core/types/types.hpp>

namespace eng::gfx::proc {

/// Desviación entera a partir de ruido `n ∈ [0,1]`: `trunc(n·(2·amp+1)) − amp`. Es la
/// perturbación estándar de bordes orgánicos (el borde de un lago o una copa deja de ser
/// un círculo matemático). Rango real `[−amp, +amp+1]`: `n` puede redondear a `1.0`
/// exacto (la interpolación del ruido redondea al más cercano), así que las cotas de los
/// atajos sin ruido de las primitivas usan `amp` por abajo y `amp+1` por arriba.
/// \tparam S escalar del ruido. \param n ruido en [0,1]. \param amp amplitud (±píxeles).
/// \return entero en `[−amp, +amp+1]`.
template <class S>
[[nodiscard]] constexpr eng::s32 jitter_from_noise(S n, eng::u8 amp) {
	using eng::math::mul_norm;
	using eng::math::scalar_traits;
	const eng::u8 span = 2u * amp + 1u;
	return scalar_traits<S>::to_int(mul_norm(n, scalar_traits<S>::from_int(span))) - amp;
}

/// **Meandro de río/arroyo**: franja de semi-ancho `width` alrededor del segmento
/// `(x0,y0)-(x1,y1)` con el contorno ondulado por ruido de baja frecuencia. Los extremos
/// son los puntos de conexión con los vecinos (el pintor los pone en el centro del borde
/// de agua), de modo que dos tramos encadenados casan sin costura.
///
/// Implementación sin división por píxel: el test de proyección `0 ≤ t ≤ len²` usa solo
/// productos, y en el tramo medio la distancia al segmento se compara como
/// `cross² ≤ w²·len²` (equivalent a `(|cross|/len) ≤ w`, el clásico `d ≤ r`, multiplicada
/// por `len²` para eliminar la raíz y la división).
/// \tparam S escalar del ruido.
template <class S>
struct RiverStroke {
	/// Extremo de entrada (borde del tile). \see x1,y1.
	eng::u16 x0 = 0;
	/// Extremo de entrada (borde del tile).
	eng::u16 y0 = 0;
	/// Extremo de salida (borde del tile).
	eng::u16 x1 = 31;
	/// Extremo de salida (borde del tile).
	eng::u16 y1 = 31;
	/// Semi-ancho del cauce en píxeles (radio; el ancho total es 2·width+1).
	eng::u16 width = 4;
	/// Color del agua (índice de paleta).
	eng::u8 color = 1;
	/// Semilla del ruido de ondulación (estable entre el cauce y su orilla).
	eng::u32 seed = 0;
	/// Amplitud de la ondulación del contorno en píxeles (±).
	eng::u8 wobble = 3;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `color` si cae dentro del cauce ondulado, si no 0.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::value_noise2;
		const eng::s32 dx = x1 - x0;
		const eng::s32 dy = y1 - y0;
		const eng::s32 len2 = dx * dx + dy * dy;
		if (len2 == 0) return 0u; // cauce degenerado (un punto): nada que pintar
		const eng::s32 px = x - x0;
		const eng::s32 py = y - y0;
		const eng::s32 t = px * dx + py * dy;
		// La ondulación acota el ancho efectivo a [width−wobble, width+wobble]: fuera de esa
		// banda la decisión no puede cambiar, así que el ruido solo se evalúa en el **borde**
		// (mismo resultado bit a bit; el 68000 se ahorra la evaluación en el relleno). El
		// ruido es 8× más grueso que el píxel: la escala es una división (tabla de recíprocos
		// del escalar, `minifloat.hpp:370`), ya solo en los píxeles del borde.
		const eng::s32 lo = eng::s32(width) - wobble;      // jitter mínimo = −wobble
		const eng::s32 hi = eng::s32(width) + wobble + 1;  // jitter máximo = +wobble+1
		auto decide = [&](eng::s32 dist2) -> eng::u8 {
			if (lo >= 1 && dist2 <= lo * lo) return color;
			if (dist2 > hi * hi) return 0u;
			const S eighth = div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(8));
			const S n = value_noise2(mul_norm(scalar_traits<S>::from_int(x), eighth),
						 mul_norm(scalar_traits<S>::from_int(y), eighth), seed);
			const eng::s32 w = width + jitter_from_noise(n, wobble);
			return (w > 0 && dist2 <= w * w) ? color : 0u;
		};
		if (t <= 0) return decide(px * px + py * py); // antes de A
		if (t >= len2) { // después de B
			const eng::s32 qx = x - x1;
			const eng::s32 qy = y - y1;
			return decide(qx * qx + qy * qy);
		}
		// Tramo medio: cross² / len² = distancia² al segmento (sin raíz ni división).
		const eng::s32 cross = px * dy - py * dx;
		if (lo >= 1 && cross * cross <= lo * lo * len2) return color;
		if (cross * cross > hi * hi * len2) return 0u;
		const S eighth = div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(8));
		const S n = value_noise2(mul_norm(scalar_traits<S>::from_int(x), eighth),
					 mul_norm(scalar_traits<S>::from_int(y), eighth), seed);
		const eng::s32 w = width + jitter_from_noise(n, wobble);
		return (w > 0 && cross * cross <= w * w * len2) ? color : 0u;
	}
};

/// **Lago irregular**: elipse `(dx/rx)² + (dy/ry)² < 1` con el borde desplazado por ruido.
/// Un lago es un río que se ensancha: el pintor superpone su orilla (misma elipse con
/// radios +1) para obtener la franja de espuma.
/// \tparam S escalar del ruido.
template <class S>
struct LakeBlob {
	/// Centro del lago en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro del lago en píxeles del tile.
	eng::u16 cy = 16;
	/// Semi-eje horizontal en píxeles.
	eng::u16 rx = 10;
	/// Semi-eje vertical en píxeles.
	eng::u16 ry = 8;
	/// Color del agua (índice de paleta).
	eng::u8 color = 1;
	/// Semilla del ruido del contorno.
	eng::u32 seed = 0;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `color` dentro de la elipse ondulada, si no 0.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::value_noise2;
		if (rx == 0u || ry == 0u) return 0u;
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::u32 rx2 = eng::u32(rx) * rx;
		const eng::u32 ry2 = eng::u32(ry) * ry;
		// Elipse en forma multiplicativa: dx²·ry² + dy²·rx² < (1 + ruido/256)·rx²·ry² (sin divisiones).
		const eng::u32 e = eng::u32(dx * dx) * ry2 + eng::u32(dy * dy) * rx2;
		const eng::u32 base = rx2 * ry2;
		// El ruido abre el umbral hasta `base·40/256`: fuera de esa banda la decisión no
		// cambia y solo se evalúa el ruido en el **borde** (mismo resultado bit a bit).
		if (e < base) return color;
		if (e >= base + ((base * 41u) >> 8u)) return 0u; // nw máximo real = 41
		const S quarter = div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(4));
		const S n = value_noise2(mul_norm(scalar_traits<S>::from_int(x), quarter),
					 mul_norm(scalar_traits<S>::from_int(y), quarter), seed);
		const eng::u8 nw = scalar_traits<S>::to_int(mul_norm(n, scalar_traits<S>::from_int(41)));
		const eng::u32 thr = base + ((base * nw) >> 8u); // apertura de hasta 40/256 del radio
		return (e < thr) ? color : 0u;
	}
};

/// **Copa de árbol**: disco de radio² `r2` con contorno deshilachado por ruido (hojas) y
/// tronco opcional debajo (`dy` en 0..5 sobre el centro, 1 píxel de ancho).
/// \tparam S escalar del ruido.
template <class S>
struct TreeCanopy {
	/// Centro del tronco/ copa en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro del tronco/ copa en píxeles del tile.
	eng::u16 cy = 16;
	/// Radio al cuadrado de la copa.
	eng::u16 r2 = 36;
	/// Color de las hojas (índice de paleta).
	eng::u8 foliage = 3;
	/// Color del tronco (índice de paleta).
	eng::u8 trunk = 4;
	/// Dibuja el tronco bajo la copa.
	bool draw_trunk = true;
	/// Semilla del ruido del contorno (una por árbol).
	eng::u32 seed = 0;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `foliage`, `trunk` o 0 (fondo).
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::value_noise2;
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::s32 d2 = dx * dx + dy * dy;
		// La perturbación es [−4, +5] (`jitter_from_noise(_, 4)`) y su test lleva el
		// guardián `d2 + jit >= 0` (valor negativo = fuera): si la decisión no puede
		// cambiar, el ruido no se evalúa (mismo resultado bit a bit). Con `d2 < 4` el
		// jitter negativo puede invalidar el test, así que esa zona también se evalúa.
		if (d2 >= 4 && d2 + 5 <= eng::s32(r2)) return foliage;
		if (d2 <= eng::s32(r2) + 4) {
			const S n = value_noise2(scalar_traits<S>::from_int(x),
						 scalar_traits<S>::from_int(y), seed);
			const eng::s32 d2j = d2 + jitter_from_noise(n, 4);
			if (d2j >= 0 && d2j <= eng::s32(r2)) return foliage;
		}
		if (draw_trunk && dy > 0 && dy < 6 && dx * dx <= 1) return trunk;
		return 0u;
	}
};

/// **Roca/peñasco**: disco pequeño de contorno irregular (mismo mecanismo que la copa,
/// con otro reparto de ruido). El pintor lo estampa 1..3 veces por tile de peñascos.
/// \tparam S escalar del ruido.
template <class S>
struct RockBlob {
	/// Centro de la roca en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro de la roca en píxeles del tile.
	eng::u16 cy = 16;
	/// Radio al cuadrado de la roca.
	eng::u16 r2 = 25;
	/// Color de la roca (índice de paleta).
	eng::u8 color = 5;
	/// Semilla del ruido del contorno (una por roca).
	eng::u32 seed = 0;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `color` si cae en la roca, si no 0.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::value_noise2;
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::s32 d2 = dx * dx + dy * dy;
		// Igual que la copa: el ruido (perturbación [−4, +5] con guardián `>= 0`) solo se
		// evalúa en el borde (y junto al centro, donde el jitter negativo puede restar).
		if (d2 >= 4 && d2 + 5 <= eng::s32(r2)) return color;
		if (d2 <= eng::s32(r2) + 4) {
			const S n = value_noise2(scalar_traits<S>::from_int(x),
						 scalar_traits<S>::from_int(y), seed);
			const eng::s32 d2j = d2 + jitter_from_noise(n, 4);
			if (d2j >= 0 && d2j <= eng::s32(r2)) return color;
		}
		return 0u;
	}
};

/// **Surcos de cultivo**: líneas paralelas de dos tonos alternos con variación por fila
/// (ruido 1D sobre el índice de surco). El pintor lo estampa sobre el material de cultivo.
/// \tparam S escalar del ruido (solo se usa en el índice de fila, no en el píxel).
template <class S>
struct CropRows {
	/// Color del surco par (índice de paleta).
	eng::u8 color_a = 6;
	/// Color del surco impar (índice de paleta).
	eng::u8 color_b = 7;
	/// Separación entre surcos en píxeles (0 = inválido, devuelve `color_a`).
	eng::u16 spacing = 4;
	/// Semilla de la variación.
	eng::u32 seed = 0;
	/// Surcos horizontales (true) o verticales (false).
	bool horizontal = true;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return uno de los dos colores de surco.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::value_noise1;
		if (spacing == 0u) return color_a;
		// `divu.w` por píxel (≈140 ciclos): generación de un tile de 32×32 = ~0,14 M ciclos, aceptable en carga; la alternativa sería precalcular una tabla de filas duplicadas.
		const eng::u16 coord = horizontal ? y : x;
		const eng::u16 row = coord / spacing;
		const S n = value_noise1(scalar_traits<S>::from_int(row), seed);
		const eng::u8 variation =
			scalar_traits<S>::to_int(mul_norm(n, scalar_traits<S>::from_int(2)));
		return ((row + variation) & 1u) != 0u ? color_a : color_b;
	}
};

/// **Segmento de muralla** (vista cenital): franja recta de `thickness` píxeles por el
/// centro del tile con almenas en el borde exterior. `dir` elige el tramo (recto o
/// esquina de dos franjas); el pintor lo deriva de los bordes `Edge::Wall` del tile.
struct WallSegment {
	/// Orientación del tramo (las cuatro esquinas dibujan las dos franjas).
	enum class Dir : eng::u8 { North, East, South, West, NorthEast, NorthWest, SouthEast, SouthWest };
	/// Tramo a pintar (el pintor lo deriva de los bordes Wall).
	Dir dir = Dir::North;
	/// Color del cuerpo de la muralla (índice de paleta).
	eng::u8 color = 8;
	/// Color de las almenas (índice de paleta).
	eng::u8 battlement = 9;
	/// Grosor de la franja en píxeles (impar = centrada exacta).
	eng::u16 thickness = 5;
	/// Centro de la franja vertical (N/S) en píxeles del tile.
	eng::u16 center_x = 16;
	/// Centro de la franja horizontal (E/W) en píxeles del tile.
	eng::u16 center_y = 16;
	/// Ancho del tile (para la banda de almenas del lado Este).
	eng::u16 tile_w = 32;
	/// Alto del tile (para la banda de almenas del lado Sur).
	eng::u16 tile_h = 32;
	/// Ancho de la banda exterior de almenas en píxeles.
	eng::u16 edge_band = 3;

	/// ¿`a` cae en la franja centrada en `center`? (mitad a cada lado; impar hacia abajo).
	/// \param a coordenada a probar. \param center centro de la franja.
	/// \return true si pertenece a la franja de `thickness`.
	[[nodiscard]] constexpr bool in_strip(eng::u16 a, eng::u16 center) const {
		const eng::u16 half = thickness / 2u;
		return a >= center - half && a < center + half + (thickness & 1u);
	}

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `color`/`battlement` en la muralla, si no 0.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		// Merlones a lo largo de la muralla: en las dos columnas (o filas) de los cantos de
		// la franja, alternos cada 4 px — el patrón que hace leer "muralla" y no "valla".
		const eng::u16 half = thickness / 2u;
		switch (dir) {
		case Dir::North:
		case Dir::South:
			if (in_strip(x, center_x)) {
				const eng::u16 a = eng::u16(center_x - half);
				const eng::u16 b = eng::u16(a + thickness - 1u);
				const bool canto = (x == a) || (x == b);
				return (canto && ((y / 4u) & 1u) != 0u) ? battlement : color;
			}
			return 0u;
		case Dir::West:
		case Dir::East:
			if (in_strip(y, center_y)) {
				const eng::u16 a = eng::u16(center_y - half);
				const eng::u16 b = eng::u16(a + thickness - 1u);
				const bool canto = (y == a) || (y == b);
				return (canto && ((x / 4u) & 1u) != 0u) ? battlement : color;
			}
			return 0u;
		case Dir::NorthEast: // muro por N y E: codo (L) por el centro, no una cruz
			if (in_strip(x, center_x) && y <= center_y) return color;
			if (in_strip(y, center_y) && x >= center_x) return color;
			return 0u;
		case Dir::NorthWest: // muro por N y W
			if (in_strip(x, center_x) && y <= center_y) return color;
			if (in_strip(y, center_y) && x <= center_x) return color;
			return 0u;
		case Dir::SouthEast: // muro por S y E
			if (in_strip(x, center_x) && y >= center_y) return color;
			if (in_strip(y, center_y) && x >= center_x) return color;
			return 0u;
		default: // Dir::SouthWest: muro por S y W
			if (in_strip(x, center_x) && y >= center_y) return color;
			if (in_strip(y, center_y) && x <= center_x) return color;
			return 0u;
		}
	}
};

/// **Torre cilíndrica** (vista cenital): disco con anillo exterior de merlones alternos y
/// tejado interior. Se estampa en las piezas `Feature::Tower`.
struct TowerTop {
	/// Centro de la torre en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro de la torre en píxeles del tile.
	eng::u16 cy = 16;
	/// Radio al cuadrado exterior.
	eng::u16 r2 = 100;
	/// Color del muro del anillo (índice de paleta).
	eng::u8 wall = 8;
	/// Color del tejado interior (índice de paleta).
	eng::u8 roof = 10;
	/// Color de los merlones (índice de paleta).
	eng::u8 merlon = 9;
	/// Color del contorno exterior (oscuro: despega la torre del fondo).
	eng::u8 outline = 0;
	/// Ancho del anillo de merlones en unidades de d².
	eng::u16 ring = 44;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `outline`/`merlon`/`wall`/`roof` o 0 fuera del disco.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::s32 d2 = dx * dx + dy * dy;
		if (d2 > r2) return 0u;
		if (d2 > eng::s32(r2) - 4) return outline; // contorno oscuro del bastión
		const eng::s32 inner = (r2 > ring) ? eng::s32(r2 - ring) : 0;
		if (d2 > inner) {
			// Merlones baratos: paridad de (dx^dy) desplazado a rango positivo.
			const eng::u16 ang = eng::u16((dx + 16) ^ (dy + 16));
			return (ang & 2u) != 0u ? merlon : wall;
		}
		return roof;
	}
};

/// **Casa** (vista cenital): rectángulo con tejado a dos aguas (triángulo) y puerta en el
/// lado inferior. El pintor la estampa en las piezas `Feature::House`.
struct HouseFootprint {
	/// Esquina superior izquierda del rectángulo en píxeles del tile.
	eng::u16 x0 = 8;
	/// Esquina superior izquierda del rectángulo en píxeles del tile.
	eng::u16 y0 = 10;
	/// Ancho del rectángulo en píxeles.
	eng::u16 w = 16;
	/// Alto del rectángulo en píxeles (>= 6: caben tejado y puerta).
	eng::u16 h = 12;
	/// Color de la pared (índice de paleta).
	eng::u8 wall = 11;
	/// Color del tejado (índice de paleta).
	eng::u8 roof = 12;
	/// Color de la puerta (índice de paleta).
	eng::u8 door = 4;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `wall`/`roof`/`door` o 0 fuera de la casa.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		if (x < x0 || x >= x0 + w || y < y0 || y >= y0 + h) return 0u;
		const eng::u16 lx = x - x0;
		const eng::u16 ly = y - y0;
		const eng::u16 roof_h = h / 3u; // gcc convierte /3 constante en multiplicación; no es divu
		if (roof_h > 0u && ly < roof_h) {
			const eng::u16 half = w / 2u;
			// La división por `roof_h` solo se paga en las filas del tejado (roof_h filas); `half` y `roof_h` son constantes del sello.
			const eng::u16 spread = (ly * half) / roof_h;
			if (lx + spread < half || lx > half + spread) return 0u;
			return roof;
		}
		if (ly >= h - 4u && lx > w / 2u - 2u && lx < w / 2u + 2u) return door;
		return wall;
	}
};

/// **Boca de cueva**: elipse oscura con borde rocoso (misma forma multiplicativa que el
/// lago). El pintor la estampa en la pieza `Feature::CaveMouth`.
struct CaveMouth {
	/// Centro de la boca en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro de la boca en píxeles del tile.
	eng::u16 cy = 20;
	/// Semi-eje horizontal en píxeles.
	eng::u16 rx = 8;
	/// Semi-eje vertical en píxeles.
	eng::u16 ry = 6;
	/// Color del interior oscuro (índice de paleta).
	eng::u8 dark = 0;
	/// Color del borde rocoso (índice de paleta).
	eng::u8 rock = 5;

	/// Evalúa el píxel `(x, y)`. \param x,y píxel del tile. \return `dark`, `rock` o 0 (fondo).
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		if (rx == 0u || ry == 0u) return 0u;
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::u32 rx2 = eng::u32(rx) * rx;
		const eng::u32 ry2 = eng::u32(ry) * ry;
		const eng::u32 e = eng::u32(dx * dx) * ry2 + eng::u32(dy * dy) * rx2;
		const eng::u32 base = rx2 * ry2;
		const eng::u32 dark_thr = (200u * base) >> 8u; // interior = 200/256 del radio
		if (e < dark_thr) return dark;
		const eng::u32 rock_thr = (280u * base) >> 8u; // orilla hasta 280/256
		return (e < rock_thr) ? rock : 0u;
	}
};

/// **Máscara circular suave** (0/1): opaca en el disco interior `r2_min`, transparente
/// fuera de `r2_max` y decidida por ruido en la corona intermedia (borde deshilachado).
/// Sirve para recortar siluetas (islas, cráteres, sprites orgánicos) con `fill_masked`.
/// La interpolación de la corona usa `inten·span > (d²−r2_min)·256`, la comparación
/// equivalente sin la división del doc (mismo resultado salvo redondeo de 1 píxel).
/// \tparam S escalar del ruido.
template <class S>
struct SoftCircleMask {
	/// Centro de la máscara en píxeles del tile.
	eng::u16 cx = 16;
	/// Centro de la máscara en píxeles del tile.
	eng::u16 cy = 16;
	/// Radio al cuadrado del interior siempre opaco.
	eng::u16 r2_min = 64;
	/// Radio al cuadrado del exterior siempre transparente (`> r2_min`).
	eng::u16 r2_max = 180;
	/// Semilla del ruido del borde.
	eng::u32 seed = 0;

	/// Evalúa la máscara en `(x, y)`. \param x,y píxel del tile. \return 1 = opaco, 0 = transparente.
	[[nodiscard]] constexpr eng::u8 operator()(eng::u16 x, eng::u16 y) const {
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::value_noise2;
		const eng::s32 dx = x - cx;
		const eng::s32 dy = y - cy;
		const eng::u32 d2 = eng::u32(dx * dx + dy * dy);
		if (d2 <= r2_min) return 1u;
		if (d2 >= r2_max) return 0u;
		const S n = value_noise2(scalar_traits<S>::from_int(x), scalar_traits<S>::from_int(y),
					 seed);
		const eng::u8 inten = scalar_traits<S>::to_int(mul_norm(n, scalar_traits<S>::from_int(255)));
		const eng::u32 span = r2_max - r2_min;
		return (eng::u32(inten) * span > (d2 - r2_min) << 8u) ? 1u : 0u;
	}
};

} // namespace eng::gfx::proc
