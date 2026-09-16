#pragma once

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
/// ## Límites por escalar (importante)
///
/// - Necesita **división** (`has_division`) para normalizar el valor de rejilla; con un
///   `Fixed` sin `operator/` no compila (a propósito).
/// - **`MiniFloat16`** (~10 bits): los valores de rejilla tienen **1024 niveles** (la
///   precisión del tipo, no más). En `fbm`, cada octava multiplica frecuencia por
///   `lacunarity` y amplitud por `gain`; con muchas octavas la amplitud **bajoflow** a
///   0 (`amp < 2^-14`) y deja de aportar. Coordenadas fuera de `[2^-14, 65504]` saturan.
/// - **`float`/`double`**: sin límites prácticos; el hash sigue siendo 32 bits.
///
/// **Estado de verificación: verificada por demo** — `demos/amiga/083_fbm_noise` usa
/// `fbm2<MiniFloat16>` para un mapa de altura en hardware (build/run/analyze OK);
/// `tests/host/060_noise` compara `value_noise`/`fbm` de `MiniFloat16` contra `double`.

#include <eng/core/interp.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/numeric_traits.hpp>
#include <eng/core/random.hpp>

namespace eng::math {

namespace noise_detail {

using u32c = __UINT32_TYPE__; // 32 bits exactos en host y m68k (igual que random.hpp)

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

/// Suelo entero de `x` (hacia −inf) sin `std`.
template <typename S>
[[nodiscard]] constexpr int ifloor(S x) {
	int i = scalar_traits<S>::to_int(x);
	if (scalar_traits<S>::from_int(i) > x) --i;
	return i;
}

/// Celda `h` -> valor en `[0,1)` con 1024 niveles (10 bits).
template <typename S>
[[nodiscard]] constexpr S unit(u32c h) {
	const int v = static_cast<int>((h >> 22) & 0x3FFu);
	return scalar_traits<S>::from_int(v) / scalar_traits<S>::from_int(1024);
}

} // namespace noise_detail

// ============================================================================
//  Value noise
// ============================================================================

/// Ruido 1D en `[0,1)`; `x` en cualquier escala (la frecuencia la fija el llamador).
template <typename S>
[[nodiscard]] constexpr S value_noise1(S x, eng::u32 seed) {
	require_division<S>();
	using namespace noise_detail;
	const int i = ifloor(x);
	const S f = x - scalar_traits<S>::from_int(i);
	const S v0 = unit<S>(hash2(static_cast<u32c>(i), static_cast<u32c>(seed)));
	const S v1 = unit<S>(hash2(static_cast<u32c>(i + 1), static_cast<u32c>(seed)));
	return lerp(v0, v1, smoothstep(f));
}

/// Ruido 2D bilineal en `[0,1)`.
template <typename S>
[[nodiscard]] constexpr S value_noise2(S x, S y, eng::u32 seed) {
	require_division<S>();
	using namespace noise_detail;
	const int ix = ifloor(x), iy = ifloor(y);
	const S fx = smoothstep(x - scalar_traits<S>::from_int(ix));
	const S fy = smoothstep(y - scalar_traits<S>::from_int(iy));
	const u32c sx = static_cast<u32c>(seed);
	const S v00 = unit<S>(hash2(static_cast<u32c>(ix), hash2(static_cast<u32c>(iy), sx)));
	const S v10 = unit<S>(hash2(static_cast<u32c>(ix + 1), hash2(static_cast<u32c>(iy), sx)));
	const S v01 = unit<S>(hash2(static_cast<u32c>(ix), hash2(static_cast<u32c>(iy + 1), sx)));
	const S v11 = unit<S>(hash2(static_cast<u32c>(ix + 1), hash2(static_cast<u32c>(iy + 1), sx)));
	return lerp(lerp(v00, v10, fx), lerp(v01, v11, fx), fy);
}

/// Ruido 3D trilineal en `[0,1)`.
template <typename S>
[[nodiscard]] constexpr S value_noise3(S x, S y, S z, eng::u32 seed) {
	require_division<S>();
	using namespace noise_detail;
	const int ix = ifloor(x), iy = ifloor(y), iz = ifloor(z);
	const S fx = smoothstep(x - scalar_traits<S>::from_int(ix));
	const S fy = smoothstep(y - scalar_traits<S>::from_int(iy));
	const S fz = smoothstep(z - scalar_traits<S>::from_int(iz));
	const u32c sx = static_cast<u32c>(seed);
	auto corner = [&](int dx, int dy, int dz) {
		const u32c h = hash2(static_cast<u32c>(ix + dx),
				     hash3(static_cast<u32c>(iy + dy), static_cast<u32c>(iz + dz), sx));
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

/// fbm 2D: suma de `octaves` octavas de value noise en `[0,1)`. `lacunarity` (p. ej. 2)
/// escala la frecuencia y `gain` (p. ej. 0.5) la amplitud por octava.
template <typename S>
[[nodiscard]] constexpr S fbm2(S x, S y, eng::u32 seed, int octaves, S lacunarity, S gain) {
	require_division<S>();
	S sum = scalar_traits<S>::zero();
	S amp = scalar_traits<S>::one();
	S freq = scalar_traits<S>::one();
	S norm = scalar_traits<S>::zero();
	for (int o = 0; o < octaves; ++o) {
		sum = sum + value_noise2(x * freq, y * freq, seed + static_cast<eng::u32>(o)) * amp;
		norm = norm + amp;
		amp = amp * gain;
		freq = freq * lacunarity;
	}
	return sum / norm;
}

} // namespace eng::math
