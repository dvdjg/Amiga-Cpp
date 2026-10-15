#pragma once

/// \file textures.hpp
/// **Texturas orgánicas tileables** (`eng::gfx::proc::tex`): materiales clásicos de
/// síntesis procedural (nubes, mármol, madera, piedra, granito, espuma, óxido, musgo)
/// construidos sobre el ruido de `eng/core/math/noise.hpp` (`fbm2`, `turbulence2`,
/// `ridged2`, `worley2_sq` — **no se reimplementan aquí**) y su cuantización a índices de
/// paleta con dither (`to_index`, `fill_texture`).
///
/// Cada textura devuelve un valor `S ∈ [0,1)` que `to_index` cuantiza a `levels`; el
/// atlas de materiales (`atlas.hpp`) las usa para sus 22 materiales.
///
/// ## Contrato de periodicidad (tileable)
///
/// `period` es el periodo del ruido **en unidades de la escala base `scale`** (0 = no
/// tileable). Para un tile de `tile_px` píxeles con `freq` ciclos, se cumple la regla
/// `scale = freq / tile_px` y `period = freq` (usa `tile_scale<S>(tile_px, freq)`):
/// con ella `ruido(px + tile_px) == ruido(px)` y dos tiles adyacentes casan sin costura
/// (verificado en `noise.hpp` con `period`). Las llamadas internas a **frecuencias
/// derivadas** (octava de detalle ×2, cristales ×2, crestas ×3, parches ÷2) propagan el
/// periodo con la misma multiplicación/división entera, así que la tileabilidad se
/// mantiene; las texturas con división (`Moss::patch_div`) exigen un `period` múltiplo.
///
/// ## Escalar
///
/// Genéricas sobre `S` (§1.11): en Amiga el válido es `MiniFloat16` (ruido + división por
/// recíprocos, `minifloat.hpp`); en host, `float`/`double`. El coste es de **generación**
/// (`octaves × value_noise2` por píxel): precalcular el tile una vez y reutilizarlo.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por tests host (`HOST-442`/`HOST-443`); no corre en hardware en la demo
/// 119 (atlas precocinado; medida en `PROCEDURAL_BITMAPS.md` §Coste).

#include <eng/core/math/noise.hpp>
#include <eng/core/math/scalar_ops.hpp>
#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/dither.hpp>
#include <eng/graphics/proc/evaluators.hpp>

namespace eng::gfx::proc::tex {

/// **Domain warping** ligero (Quilez): desplaza `x`/`y` por otro fBm de 3 octavas; rompe
/// la regularidad del ruido base (la piedra y el óxido lo usan). El desplazamiento usa
/// el mismo `period`, así que la deformación también es tileable.
/// \tparam S escalar del ruido. \param x,y coordenadas de ruido (entrada/salida, in-place).
/// \param seed semilla del warp. \param strength amplitud del desplazamiento (en unidades de ruido).
/// \param period periodo del warp (0 = no tileable).
template <class S>
constexpr void domain_warp(S& x, S& y, eng::u32 seed, S strength, int period = 0) {
	using eng::math::div_norm;
	using eng::math::fbm2;
	using eng::math::fbm2_cached;
	using eng::math::mul_norm;
	using eng::math::scalar_traits;
	const S one = scalar_traits<S>::one();
	const S two = scalar_traits<S>::from_int(2);
	const S half = div_norm(one, two);
	const S wx = fbm2_cached(x, y, seed, 3, two, half, period) - half;
	const S wy = fbm2_cached(x + scalar_traits<S>::from_int(5), y + scalar_traits<S>::from_int(3),
			  seed + 17u, 3, two, half, period) -
		     half;
	x = x + mul_norm(wx, strength);
	y = y + mul_norm(wy, strength);
}

/// Plegado triangular de un valor `v ≥ 0` a `[0,1]`: `fract(v)` seguido de `2s` (subida)
/// o `2−2s` (bajada). Es el "seno barato" de las vetas de mármol/madera (sin `sin`).
/// \tparam S escalar. \param v valor no negativo (p. ej. `x + turbulencia`).
/// \return valor en `[0,1]` con pico en las fracciones 0,5.
template <class S>
[[nodiscard]] constexpr S fold_unit(S v) {
	using eng::math::div_norm;
	using eng::math::mul_norm;
	using eng::math::scalar_traits;
	const S half = div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(2));
	const S s = v - scalar_traits<S>::from_int(scalar_traits<S>::to_int(v)); // fract (v ≥ 0)
	return (s < half) ? mul_norm(s, scalar_traits<S>::from_int(2))
			  : mul_norm(scalar_traits<S>::one() - s, scalar_traits<S>::from_int(2));
}

/// **Nubes / humo / niebla**: fBm clásico (base de nubes, agua, arena, nieve y cultivos).
/// \tparam S escalar del ruido.
template <class S>
struct Clouds {
	/// Semilla del ruido (cada material usa una distinta).
	eng::u32 seed = 0;
	/// Octavas de fBm (más = más detalle y coste).
	eng::u8 octaves = 5;
	/// Frecuencia espacial (`tile_scale<S>` para tiles tileables).
	S scale {};
	/// Periodo en unidades de `scale` (0 = no tileable; ver contrato del fichero).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (cuantizar con `to_index`).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::fbm2;
	using eng::math::fbm2_cached;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		const S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		const S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		return fbm2_cached(x, y, seed, octaves, two, div_norm(scalar_traits<S>::one(), two), period);
	}
};

/// **Mármol**: venas = turbulencia sobre un eje + plegado (Perlin clásico). Tileable con
/// `freq = 1` (el plegado repite cada unidad de `x`).
/// \tparam S escalar del ruido.
template <class S>
struct Marble {
	/// Semilla del ruido.
	eng::u32 seed = 0;
	/// Frecuencia espacial (con `period = 1` para tileable).
	S scale {};
	/// Fuerza de las venas (cuánto desplaza la turbulencia el eje del plegado).
	S turbulence_str {};
	/// Octavas de turbulencia.
	eng::u8 octaves = 5;
	/// Periodo en unidades de `scale` (0 = no tileable).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (venas con contraste cuadrático).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::turbulence2;
	using eng::math::turbulence2_cached;
		const S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		const S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		const S t = turbulence2_cached(x, y, seed, octaves, two, div_norm(scalar_traits<S>::one(), two),
					period);
		const S folded = fold_unit(x + mul_norm(t, turbulence_str));
		return mul_norm(folded, folded); // contraste (pico estrecho en la vena)
	}
};

/// **Madera (veta de tabla)**: turbulencia + anillos direccionales a lo largo de `y`
/// (tileable en ambos ejes: el desfase del plegado es entero por tile). La versión con
/// anillos concéntricos de tronco no es periódica y no se ofrece aquí.
/// \tparam S escalar del ruido.
template <class S>
struct Wood {
	/// Semilla del ruido.
	eng::u32 seed = 0;
	/// Frecuencia espacial.
	S scale {};
	/// Fuerza de la ondulación de la veta (relativa al espaciado de anillos).
	S turbulence_str {};
	/// Número de anillos por unidad de ruido (entero para garantizar la periodicidad).
	eng::u8 ring_freq = 10;
	/// Periodo en unidades de `scale` (0 = no tileable).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` con bandas de veta.
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::turbulence2;
	using eng::math::turbulence2_cached;
		const S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		const S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		const S t = turbulence2_cached(x, y, seed, 4, two, div_norm(scalar_traits<S>::one(), two), period);
		const S wobble = mul_norm(t, turbulence_str); // desfase de la veta por octava de ruido
		const S v = mul_norm(y + wobble, scalar_traits<S>::from_int(ring_freq));
		return fold_unit(v);
	}
};

/// **Piedra / roca**: `domain_warp` + `ridged2` (crestas) + fBm de detalle. Es el
/// material de muros, caminos y cuevas.
/// \tparam S escalar del ruido.
template <class S>
struct Stone {
	/// Semilla del ruido.
	eng::u32 seed = 0;
	/// Frecuencia espacial.
	S scale {};
	/// Octavas del `ridged2` principal.
	eng::u8 octaves = 5;
	/// Multiplicador entero de la octava de detalle (2 = ×2; propaga el periodo).
	eng::u8 detail_mul = 2;
	/// Periodo en unidades de `scale` (0 = no tileable).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (3/4 crestas + 1/4 detalle).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::fbm2;
	using eng::math::fbm2_cached;
		using eng::math::lerp;
		using eng::math::mul_norm;
		using eng::math::ridged2;
	using eng::math::ridged2_cached;
		using eng::math::scalar_traits;
		S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		const S half = div_norm(scalar_traits<S>::one(), two);
		domain_warp(x, y, seed, div_norm(scalar_traits<S>::one(), scalar_traits<S>::from_int(3)),
			    period);
		const S r = ridged2_cached(x, y, seed, octaves, two, half, period);
		const S detail_mul_s = scalar_traits<S>::from_int(detail_mul);
		const int detail_period = (period > 0) ? period * detail_mul : 0;
		const S f = fbm2_cached(mul_norm(x, detail_mul_s), mul_norm(y, detail_mul_s), seed + 31u, 3, two,
				 half, detail_period);
		return lerp(f, r, div_norm(scalar_traits<S>::from_int(3), scalar_traits<S>::from_int(4)));
	}
};

/// **Granito**: fBm base + cristales de Worley a doble frecuencia (celdas visibles).
/// \tparam S escalar del ruido.
template <class S>
struct Granite {
	/// Semilla del ruido.
	eng::u32 seed = 0;
	/// Frecuencia espacial de la base.
	S scale {};
	/// Multiplicador entero de la escala de cristales (2 = ×2; propaga el periodo).
	eng::u8 crystal_mul = 2;
	/// Octavas del fBm base.
	eng::u8 octaves = 4;
	/// Periodo en unidades de `scale` (0 = no tileable).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (2/3 base + 1/3 cristales).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::fbm2;
	using eng::math::fbm2_cached;
		using eng::math::lerp;
		using eng::math::max;
		using eng::math::min;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::worley2_sq;
		const S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		const S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		const S half = div_norm(scalar_traits<S>::one(), two);
		const S base = fbm2_cached(x, y, seed, octaves, two, half, period);
		const S crystal_mul_s = scalar_traits<S>::from_int(crystal_mul);
		const int cper = (period > 0) ? period * crystal_mul : 0;
		const S cell = worley2_sq(mul_norm(x, crystal_mul_s), mul_norm(y, crystal_mul_s),
					  seed + 7u, cper);
		const S crystals = scalar_traits<S>::one() - min(mul_norm(cell, two), scalar_traits<S>::one());
		return lerp(crystals, base, div_norm(two, scalar_traits<S>::from_int(3)));
	}
};

/// **Espuma / burbujas / adoquín**: Worley con umbral de interior; los bordes de celda
/// y los huecos dan la textura. Con `threshold` alto se convierte en ladrillo/adoquín.
/// \tparam S escalar del ruido.
template <class S>
struct Foam {
	/// Semilla del ruido (posiciones de las burbujas).
	eng::u32 seed = 0;
	/// Frecuencia espacial (tamaño de burbuja).
	S scale {};
	/// Umbral `d² < threshold` del interior de la burbuja (0 = solo bordes).
	S threshold = eng::math::scalar_traits<S>::zero();
	/// Periodo en unidades de `scale` (0 = no tileable).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (borde luminoso + interior suave).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::max;
		using eng::math::min;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::worley2_sq;
		const S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		const S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S d = worley2_sq(x, y, seed, period);
		const S one = scalar_traits<S>::one();
		const S edge = one - min(mul_norm(d, scalar_traits<S>::from_int(3)), one);
		const S fill = (threshold > scalar_traits<S>::zero())
				       ? one - div_norm(d, threshold) // solo si el umbral es usable
				       : scalar_traits<S>::zero();
		const S inside = (d < threshold) ? fill : scalar_traits<S>::zero();
		return max(mul_norm(edge, div_norm(one, scalar_traits<S>::from_int(2))), inside);
	}
};

/// **Óxido / corrosión**: turbulencia warpeada + ridged a triple frecuencia (costras).
/// \tparam S escalar del ruido.
template <class S>
struct Rust {
	/// Semilla del ruido.
	eng::u32 seed = 0;
	/// Frecuencia espacial.
	S scale {};
	/// Multiplicador entero de las crestas (3 = ×3; propaga el periodo).
	eng::u8 ridge_mul = 3;
	/// Periodo en unidades de `scale` (0 = no tileable).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (2/3 turbulencia + 1/3 crestas).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::ridged2;
	using eng::math::ridged2_cached;
		using eng::math::scalar_traits;
		using eng::math::turbulence2;
	using eng::math::turbulence2_cached;
		S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		const S half = div_norm(scalar_traits<S>::one(), two);
		domain_warp(x, y, seed, half, period);
		const S t = turbulence2_cached(x, y, seed, 4, two, half, period);
		const S ridge_mul_s = scalar_traits<S>::from_int(ridge_mul);
		const int rper = (period > 0) ? period * ridge_mul : 0;
		const S r = ridged2_cached(mul_norm(x, ridge_mul_s), mul_norm(y, ridge_mul_s), seed + 11u, 3,
				    two, half, rper);
		// t·2/3 + r·1/3 = lerp(r, t, 2/3)
		return (r + mul_norm(t - r, div_norm(two, scalar_traits<S>::from_int(3))));
	}
};

/// **Musgo / liquen**: fBm enmascarado por parches Worley de baja frecuencia (÷
/// `patch_div`; el `period` debe ser múltiplo de `patch_div` para seguir siendo tileable).
/// \tparam S escalar del ruido.
template <class S>
struct Moss {
	/// Semilla del ruido.
	eng::u32 seed = 0;
	/// Frecuencia espacial del fBm.
	S scale {};
	/// Divisor entero de la escala de parches (2 = mitad de frecuencia; propaga el periodo).
	eng::u8 patch_div = 2;
	/// Periodo en unidades de `scale` (0 = no tileable; usar múltiplo de `patch_div`).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (fBm × máscara de parches).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::fbm2;
	using eng::math::fbm2_cached;
		using eng::math::min;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		using eng::math::worley2_sq;
		const S x = mul_norm(scalar_traits<S>::from_int(px), scale);
		const S y = mul_norm(scalar_traits<S>::from_int(py), scale);
		const S two = scalar_traits<S>::from_int(2);
		const S half = div_norm(scalar_traits<S>::one(), two);
		const S f = fbm2_cached(x, y, seed, 4, two, half, period);
		const S div_s = scalar_traits<S>::from_int(patch_div);
		const int pper = (period >= patch_div && patch_div > 0) ? period / patch_div : 0;
		const S c = worley2_sq(div_norm(x, div_s), div_norm(y, div_s), seed + 19u, pper);
		const S patches = scalar_traits<S>::one() - min(mul_norm(c, two), scalar_traits<S>::one());
		return mul_norm(f, patches);
	}
};

/// **Piedra erosionada**: composición de `Stone` + `Granite` + `Moss` (el musgo solo
/// donde la piedra es "hueca", valores bajos). El caso de composición multi-capa.
/// \tparam S escalar del ruido.
template <class S>
struct WeatheredStone {
	/// Semilla del ruido (las capas derivan `seed+100`, `seed+200`).
	eng::u32 seed = 0;
	/// Frecuencia espacial de las tres capas.
	S scale {};
	/// Periodo en unidades de `scale` (0 = no tileable; múltiplo de 2 para el musgo).
	int period = 0;

	/// Evalúa el valor de la textura en `(px, py)`.
	/// \param px,py píxel del tile. \return valor `[0,1)` (piedra 3/5 + granito 1/5 + musgo 1/5).
	[[nodiscard]] constexpr S operator()(eng::u16 px, eng::u16 py) const {
		using eng::math::div_norm;
		using eng::math::mul_norm;
		using eng::math::scalar_traits;
		const Stone<S> st {seed, scale, 5, 2, period};
		const Granite<S> gr {seed + 100u, scale, 2, 4, period};
		const Moss<S> mo {seed + 200u, scale, 2, period};
		const S s = st(px, py);
		const S g = gr(px, py);
		const S m = mo(px, py);
		const S five = scalar_traits<S>::from_int(5);
		const S moss_mask = scalar_traits<S>::one() - s;
		return mul_norm(s, div_norm(scalar_traits<S>::from_int(3), five)) +
		       mul_norm(g, div_norm(scalar_traits<S>::one(), five)) +
		       mul_norm(mul_norm(m, moss_mask), div_norm(scalar_traits<S>::one(), five));
	}
};

/// Cuantiza el valor `[0,1)` de una textura a un índice de paleta, con o sin dither
/// ordenado. La conversión a intensidad usa `mul_norm(value, 255)` y `to_int`.
/// \tparam S escalar de la textura. \param value salida de la textura (se satura de facto a 255).
/// \param levels niveles de paleta (2..32). \param x,y píxel (módulo 8 para el dither).
/// \param dither activa el dither ordenado. \return índice 0..levels-1.
template <class S>
[[nodiscard]] constexpr eng::u8 to_index(S value, eng::u8 levels, eng::u16 x, eng::u16 y, bool dither = true) {
	using eng::math::mul_norm;
	using eng::math::scalar_traits;
	const eng::u8 inten = scalar_traits<S>::to_int(mul_norm(value, scalar_traits<S>::from_int(255)));
	return dither ? quantize_dither(inten, x, y, levels) : quantize_plain(inten, levels);
}

/// Rellena `buf` muestreando una textura y cuantizándola con dither (el paso que
/// convierte el material continuo en índices listos para el atlas).
/// \tparam W,H geometría del tile. \tparam TexFn callable `S(u16 px, u16 py)`.
/// \param buf buffer destino. \param tex textura a muestrear. \param levels niveles de paleta.
/// \param dither activa el dither ordenado.
template <eng::u16 W, eng::u16 H, class TexFn>
constexpr void fill_texture(BitmapBuffer<W, H>& buf, TexFn&& tex, eng::u8 levels = 16u, bool dither = true) {
	for (eng::u16 y = 0; y < H; ++y) {
		for (eng::u16 x = 0; x < W; ++x) {
			buf.at(x, y) = to_index(tex(x, y), levels, x, y, dither);
		}
	}
}

} // namespace eng::gfx::proc::tex
