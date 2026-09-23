#pragma once
#include <eng/core/scalar_fwd.hpp>

/// \file noise.hpp
/// **Ruido procedural value-noise** 1D/2D/3D y **fbm** (suma de octavas), genérico
/// sobre el escalar `S` como el resto de la librería (`MiniFloat16`, `float`,
/// `double`…). Reutiliza `rotl32` de `core/random.hpp` para combinar ejes y usa un
/// finalizador splitmix32 como hash de celda (los bits altos son los que se leen, así
/// que hace falta buena difusión; en 68000 paga dos `__mulsi3` por hash).
///
/// Value noise: cada punto de la rejilla tiene un valor pseudoaleatorio en `[0,1)` y el
/// resultado interpola con `smoothstep` entre los vecinos. Es más barato que Perlin y,
/// sumado en octavas (fbm), da terrenos, nubes y plasma convincentes.
///
/// **Ruido periódico (tileable)**: con `period > 0` la rejilla se envuelve módulo
/// `period` en cada eje, de modo que `noise(x + period) == noise(x)` y una textura se
/// puede repetir sin costura. En `fbm` se asume `lacunarity == 2` para que todas las
/// octavas compartan el periodo del dominio.
///
/// ## Límites por escalar (importante)
///
/// - Normaliza el valor de rejilla con `div_norm` (la división explícita del escalar), así
///   que sirve también con `Fixed`. El límite real es el **rango**: la rejilla tiene 1024
///   niveles y el fixed 4.12 (±8) no los representa, así que se rechaza en compilación;
///   `Fixed<s32,E>` (host/68020) sí vale.
/// - **`MiniFloat16`** (~10 bits): los valores de rejilla tienen **1024 niveles** (la
///   precisión del tipo, no más). El índice de celda es exacto hasta `|coord| <= 2048`
///   (por encima, la mantisa de 10 bits deja de representar enteros); una coordenada
///   **constante** fuera de ese rango **falla al compilar**. En `fbm`, cada octava
///   multiplica frecuencia por `lacunarity` y amplitud por `gain`; con muchas octavas la
///   amplitud **bajoflow** a 0 (`amp < 2^-14`) y deja de aportar.
/// - **`float`/`double`**: sin límites prácticos; el hash sigue siendo 32 bits.
///
/// **Estado de verificación: verificada por demo** — `demos/amiga/083_fbm_noise` usa
/// `fbm2<MiniFloat16>` para un mapa de altura en hardware (build/run/analyze OK);
/// `tests/host/060_noise` compara `value_noise`/`fbm` de `MiniFloat16` contra `double`;
/// `tests/host/135_scalar_matrix` ejercita `value_noise1`/`fbm1` con `Fixed<s32,12>`.

#include <eng/core/interp.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/core/random.hpp>
#include <eng/core/scalar_ops.hpp>

namespace eng::math {

/// Límite de coordenada del ruido por escalar: por encima, el índice de celda deja de
/// ser exacto y la textura "se congela". `MiniFloat16` solo representa enteros exactos
/// hasta 2048; los demás escalares tienen margen de sobra.
template <typename S>
struct noise_traits {
	static constexpr double max_coord = 1.0e30;
};

namespace noise_detail {

using u32c = __UINT32_TYPE__; // 32 bits exactos en host y m68k (igual que random.hpp)

// Diagnóstico de dominio en compilación (mismo patrón que minifloat_math): una constante
// fuera de rango "llama" a una función no-constexpr y el compilador falla nombrando el
// límite; en runtime la rama `if consteval` no se ejecuta.
void noise_domain_coord_out_of_range();
void noise_domain_octaves_must_be_positive();

/// Hash entero determinista (finalizador splitmix32). Se lee la parte ALTA del hash
/// (`unit`), así que hace falta buena difusión; un mezclado solo-xor deja estructura en
/// los bits altos y produce bandas. En 68000 los dos productos son `__mulsi3`, pero el
/// hash se usa pocas veces por muestra (y en una demo de ruido, en el precalculado).
[[nodiscard]] constexpr u32c hash1(u32c x) {
	x += 0x9E3779B9u;
	x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
	x = (x ^ (x >> 13)) * 0xC2B2AE35u;
	x ^= x >> 16;
	return x;
}
[[nodiscard]] constexpr u32c hash2(u32c x, u32c y) {
	return hash1(x ^ eng::detail::rotl32(hash1(y), 16u));
}
[[nodiscard]] constexpr u32c hash3(u32c x, u32c y, u32c z) {
	return hash1(x ^ eng::detail::rotl32(hash2(y, z), 8u));
}

/// Envuelve un índice de celda al periodo `p` (`p <= 0` = sin envolver). Para `p`
/// potencia de dos usa máscara (barato); si no, módulo.
[[nodiscard]] constexpr int lwrap(int i, int p) {
	if (p <= 0) return i;
	if ((p & (p - 1)) == 0) return i & (p - 1);
	const int r = i % p;
	return r < 0 ? r + p : r;
}

/// Suelo entero de `x` (hacia −inf) sin `std`.
template <typename S>
[[nodiscard]] constexpr int ifloor(S x) {
	int i = scalar_traits<S>::to_int(x);
	if (scalar_traits<S>::from_int(i) > x) --i;
	return i;
}

/// Comprueba en compilación (si `x` es constante) que la coordenada está en el rango
/// donde el índice de celda es exacto para el escalar.
template <typename S>
constexpr void check_coord(S x) {
	if consteval {
		if (!in_range(x, -noise_traits<S>::max_coord, noise_traits<S>::max_coord))
			noise_domain_coord_out_of_range();
	}
}

/// El valor de rejilla tiene 1024 niveles: `S` debe poder representar hasta 1023. Es lo
/// que descarta el fixed 4.12 (rango ±8) en compilación; `Fixed<s32,E>` o los de coma
/// flotante lo cumplen.
template <typename S>
constexpr void require_grid_range() {
	require_range<S, -1024.0, 1024.0>();
}

/// Celda `h` -> valor en `[0,1)` con 1024 niveles (10 bits). La normalización usa
/// `div_norm` (el fixed no tiene `operator/`). El corte `if constexpr` evita instanciar
/// `from_int(1024)` cuando el rango no llega: solo se emite el diagnóstico de dominio.
template <typename S>
[[nodiscard]] constexpr S unit(u32c h) {
	if constexpr (!range_fits<S, -1024.0, 1024.0>()) {
		require_grid_range<S>();
		return scalar_traits<S>::zero();
	} else {
		const int v = static_cast<int>((h >> 22) & 0x3FFu);
		return div_norm(scalar_traits<S>::from_int(v),
				scalar_traits<S>::from_int(1024));
	}
}

} // namespace noise_detail

// ============================================================================
//  Value noise
// ============================================================================

/// Ruido 1D en `[0,1)`; `x` en cualquier escala (la frecuencia la fija el llamador).
/// `period > 0` lo hace periódico en ese dominio.
template <typename S>
[[nodiscard]] constexpr S value_noise1(S x, eng::u32 seed, int period = 0) {
	noise_detail::check_coord<S>(x);
	using namespace noise_detail;
	const int i = ifloor(x);
	const S f = x - scalar_traits<S>::from_int(i);
	const S v0 = unit<S>(hash2(static_cast<u32c>(lwrap(i, period)), static_cast<u32c>(seed)));
	const S v1 = unit<S>(hash2(static_cast<u32c>(lwrap(i + 1, period)), static_cast<u32c>(seed)));
	return lerp(v0, v1, smoothstep(f));
}

/// Ruido 2D bilineal en `[0,1)`. `period > 0` lo hace periódico en `[0, period)²`.
template <typename S>
[[nodiscard]] constexpr S value_noise2(S x, S y, eng::u32 seed, int period = 0) {
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	using namespace noise_detail;
	const int ix = ifloor(x), iy = ifloor(y);
	const S fx = smoothstep(x - scalar_traits<S>::from_int(ix));
	const S fy = smoothstep(y - scalar_traits<S>::from_int(iy));
	const u32c sx = static_cast<u32c>(seed);
	const int x0 = lwrap(ix, period), x1 = lwrap(ix + 1, period);
	const int y0 = lwrap(iy, period), y1 = lwrap(iy + 1, period);
	const S v00 = unit<S>(hash2(static_cast<u32c>(x0), hash2(static_cast<u32c>(y0), sx)));
	const S v10 = unit<S>(hash2(static_cast<u32c>(x1), hash2(static_cast<u32c>(y0), sx)));
	const S v01 = unit<S>(hash2(static_cast<u32c>(x0), hash2(static_cast<u32c>(y1), sx)));
	const S v11 = unit<S>(hash2(static_cast<u32c>(x1), hash2(static_cast<u32c>(y1), sx)));
	return lerp(lerp(v00, v10, fx), lerp(v01, v11, fx), fy);
}

/// Ruido 3D trilineal en `[0,1)`. `period > 0` lo hace periódico.
template <typename S>
[[nodiscard]] constexpr S value_noise3(S x, S y, S z, eng::u32 seed, int period = 0) {
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	noise_detail::check_coord<S>(z);
	using namespace noise_detail;
	const int ix = ifloor(x), iy = ifloor(y), iz = ifloor(z);
	const S fx = smoothstep(x - scalar_traits<S>::from_int(ix));
	const S fy = smoothstep(y - scalar_traits<S>::from_int(iy));
	const S fz = smoothstep(z - scalar_traits<S>::from_int(iz));
	const u32c sx = static_cast<u32c>(seed);
	auto corner = [&](int dx, int dy, int dz) {
		const u32c h =
			hash2(static_cast<u32c>(lwrap(ix + dx, period)),
			      hash3(static_cast<u32c>(lwrap(iy + dy, period)),
				    static_cast<u32c>(lwrap(iz + dz, period)), sx));
		return unit<S>(h);
	};
	const S c00 = lerp(corner(0, 0, 0), corner(1, 0, 0), fx);
	const S c10 = lerp(corner(0, 1, 0), corner(1, 1, 0), fx);
	const S c01 = lerp(corner(0, 0, 1), corner(1, 0, 1), fx);
	const S c11 = lerp(corner(0, 1, 1), corner(1, 1, 1), fx);
	return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
}

// ============================================================================
//  fbm (suma de octavas)
// ============================================================================

/// Comprueba en compilación que `octaves >= 1` (si es constante).
constexpr void check_octaves(int octaves) {
	if consteval {
		if (octaves < 1) noise_detail::noise_domain_octaves_must_be_positive();
	}
}

/// fbm 1D: suma de `octaves` octavas de value noise en `[0,1)`. `period > 0` lo hace
/// periódico (asume `lacunarity == 2`).
template <typename S>
[[nodiscard]] constexpr S fbm1(S x, eng::u32 seed, int octaves, S lacunarity, S gain,
			       int period = 0) {
	check_octaves(octaves);
	noise_detail::check_coord<S>(x);
	S sum = scalar_traits<S>::zero();
	S amp = scalar_traits<S>::one();
	S freq = scalar_traits<S>::one();
	S norm = scalar_traits<S>::zero();
	int p = period;
	for (int o = 0; o < octaves; ++o) {
		const S xf = mul_norm(x, freq);
		sum = sum + mul_norm(value_noise1(xf, seed + static_cast<eng::u32>(o), p), amp);
		norm = norm + amp;
		amp = mul_norm(amp, gain);
		freq = mul_norm(freq, lacunarity);
		if (p > 0) p <<= 1;
	}
	return div_norm(sum, norm);
}

/// fbm 2D: suma de `octaves` octavas de value noise en `[0,1)`. `lacunarity` (p. ej. 2)
/// escala la frecuencia y `gain` (p. ej. 0.5) la amplitud por octava. `period > 0` lo
/// hace periódico (asume `lacunarity == 2`).
template <typename S>
[[nodiscard]] constexpr S fbm2(S x, S y, eng::u32 seed, int octaves, S lacunarity, S gain,
			       int period = 0) {
	check_octaves(octaves);
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	S sum = scalar_traits<S>::zero();
	S amp = scalar_traits<S>::one();
	S freq = scalar_traits<S>::one();
	S norm = scalar_traits<S>::zero();
	int p = period;
	for (int o = 0; o < octaves; ++o) {
		const S xf = mul_norm(x, freq), yf = mul_norm(y, freq);
		sum = sum + mul_norm(value_noise2(xf, yf, seed + static_cast<eng::u32>(o), p), amp);
		norm = norm + amp;
		amp = mul_norm(amp, gain);
		freq = mul_norm(freq, lacunarity);
		if (p > 0) p <<= 1;
	}
	return div_norm(sum, norm);
}

/// fbm 3D: suma de `octaves` octavas de value noise en `[0,1)`. `period > 0` lo hace
/// periódico (asume `lacunarity == 2`).
template <typename S>
[[nodiscard]] constexpr S fbm3(S x, S y, S z, eng::u32 seed, int octaves, S lacunarity,
			       S gain, int period = 0) {
	check_octaves(octaves);
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	noise_detail::check_coord<S>(z);
	S sum = scalar_traits<S>::zero();
	S amp = scalar_traits<S>::one();
	S freq = scalar_traits<S>::one();
	S norm = scalar_traits<S>::zero();
	int p = period;
	for (int o = 0; o < octaves; ++o) {
		const S xf = mul_norm(x, freq), yf = mul_norm(y, freq), zf = mul_norm(z, freq);
		sum = sum +
		      mul_norm(value_noise3(xf, yf, zf, seed + static_cast<eng::u32>(o), p), amp);
		norm = norm + amp;
		amp = mul_norm(amp, gain);
		freq = mul_norm(freq, lacunarity);
		if (p > 0) p <<= 1;
	}
	return div_norm(sum, norm);
}

// ============================================================================
//  Worley (cellular) y variantes de fbm
// ============================================================================

/// **Worley/cellular 2D** en distancia al cuadrado: para cada celda se coloca un punto
/// de característica pseudoaleatorio (dos valores de `hash`) y se toma la distancia
/// mínima entre la celda y sus 8 vecinas. Devuelve la distancia **al cuadrado** (sin
/// `sqrt`), en `[0, ~2]`; para distancia euclídea usa `worley2`.
template <typename S>
[[nodiscard]] constexpr S worley2_sq(S x, S y, eng::u32 seed, int period = 0) {
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	using namespace noise_detail;
	const int ix = ifloor(x), iy = ifloor(y);
	const S fx = x - scalar_traits<S>::from_int(ix);
	const S fy = y - scalar_traits<S>::from_int(iy);
	S best = scalar_traits<S>::from_int(4); // mayor que cualquier distancia de 3x3
	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			const int cx = lwrap(ix + dx, period), cy = lwrap(iy + dy, period);
			const u32c h = hash2(static_cast<u32c>(cx),
					     hash2(static_cast<u32c>(cy), static_cast<u32c>(seed)));
			const S ox = unit<S>(h);
			const S oy = unit<S>(hash1(h));
			const S px = scalar_traits<S>::from_int(dx) + ox - fx;
			const S py = scalar_traits<S>::from_int(dy) + oy - fy;
			const S d = mul_norm(px, px) + mul_norm(py, py);
			if (d < best) {
				best = d;
			}
		}
	}
	return best;
}

/// Worley/cellular 2D en distancia euclídea (requiere `sqrt` del escalar).
template <typename S>
[[nodiscard]] constexpr S worley2(S x, S y, eng::u32 seed, int period = 0) {
	return scalar_sqrt<S>::op(worley2_sq(x, y, seed, period));
}

/// **Turbulencia** 2D (`|2·n−1|` sumado en octavas): nubes/humo con crestas suaves.
/// Devuelve `[0,1]`.
template <typename S>
[[nodiscard]] constexpr S turbulence2(S x, S y, eng::u32 seed, int octaves, S lacunarity,
				      S gain, int period = 0) {
	check_octaves(octaves);
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	S sum = scalar_traits<S>::zero();
	S amp = scalar_traits<S>::one();
	S freq = scalar_traits<S>::one();
	S norm = scalar_traits<S>::zero();
	int p = period;
	for (int o = 0; o < octaves; ++o) {
		const S n = value_noise2(mul_norm(x, freq), mul_norm(y, freq),
					 seed + static_cast<eng::u32>(o), p);
		const S b = abs(mul_norm(scalar_traits<S>::from_int(2), n) - scalar_traits<S>::one());
		sum = sum + mul_norm(b, amp);
		norm = norm + amp;
		amp = mul_norm(amp, gain);
		freq = mul_norm(freq, lacunarity);
		if (p > 0) p <<= 1;
	}
	return div_norm(sum, norm);
}

/// **Ridged** 2D (crestas afiladas): `r = 1−|2n−1|`, sumado como `r²·amp`. Devuelve
/// `[0,1]`.
template <typename S>
[[nodiscard]] constexpr S ridged2(S x, S y, eng::u32 seed, int octaves, S lacunarity, S gain,
				  int period = 0) {
	check_octaves(octaves);
	noise_detail::check_coord<S>(x);
	noise_detail::check_coord<S>(y);
	S sum = scalar_traits<S>::zero();
	S amp = scalar_traits<S>::one();
	S freq = scalar_traits<S>::one();
	S norm = scalar_traits<S>::zero();
	int p = period;
	for (int o = 0; o < octaves; ++o) {
		const S n = value_noise2(mul_norm(x, freq), mul_norm(y, freq),
					 seed + static_cast<eng::u32>(o), p);
		const S r = scalar_traits<S>::one() -
			    abs(mul_norm(scalar_traits<S>::from_int(2), n) - scalar_traits<S>::one());
		sum = sum + mul_norm(mul_norm(r, r), amp);
		norm = norm + amp;
		amp = mul_norm(amp, gain);
		freq = mul_norm(freq, lacunarity);
		if (p > 0) p <<= 1;
	}
	return div_norm(sum, norm);
}

} // namespace eng::math
