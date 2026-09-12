#pragma once

/// \file dualtab.hpp
/// Tabla de color del fuego (`fire-rgb`), generada en **compile-time con C++23**.
/// Sustituye a `data/gen-dualtab.py` del original (que la hornea a un `.c`); misma
/// tabla, pero calculada por el compilador y sin tooling de Python.
///
/// ## Qué es `dualtab`
///
/// Cada entrada es una palabra de 32 bits:
///
///   ┌──────────────── 32 bits ────────────────┐
///   │  palabra alta: CALOR   │ palabra baja: COLOR (HAM) │
///   └────────────────────────┴───────────────────────────┘
///
/// - **Calor** = `val * 4`, con `val = i * 63 / 256` (0..62). El fuego lo usa como
///   realimentación: la suma de 4 vecinos indexa la tabla y la palabra alta es el
///   calor nuevo.
/// - **Color** = el patrón HAM "scrambled" del color de la paleta en `val`. El fuego
///   lo vuelca a `chunky` (→ C2P → display HAM). El `scramble` reparte los 4 bits de
///   cada canal RGB de un color en 4 píxeles HAM:
///
///   `[r0 g0 b0 b0] [r1 g1 b1 b1] [r2 g2 b2 b2] [r3 g3 b3 b3]`
///
///   (4 bits por píxel HAM: 1 de R, 1 de G y 2 de B, como exige el modo HAM).
///
/// La paleta es la primera columna de `data/colors-red.png` (64 colores RGB444).
///
/// Uso: `fire_rgb::kDualTab.v[i]` (o `fire_rgb::dual_entry(i)`).

#include <eng/core/types.hpp>

namespace fire_rgb {

/// Paleta de fuego (RGB444): primera columna de `data/colors-red.png`.
inline constexpr eng::u16 kPalette[64] = {
	0,    256,  512,  768,  1024, 1296, 1552, 1824, 2080, 2096, 2352, 2624, 2880, 2896, 3152, 3168,
	3184, 3440, 3712, 3728, 3984, 3984, 4000, 4000, 4016, 4016, 4032, 4032, 4048, 4048, 4064, 4064,
	4080, 4080, 3824, 3824, 3568, 3568, 3312, 3312, 3056, 3056, 2800, 2800, 2544, 2544, 2288, 2288,
	2032, 2032, 1776, 1776, 1520, 1520, 1264, 1264, 1008, 1008, 752,  752,  496,  496,  240,  240,
};

/// HAM "scramble" de un color RGB444: 16 bits = 4 píxeles HAM de 4 bits, donde el
/// píxel `k` lleva el bit `k` (de MSB a LSB) de R, G y B (B duplicado).
constexpr eng::u16 scramble(eng::u16 c) {
	eng::u16 out = 0;
	for (eng::u8 k = 0; k < 4; ++k) {
		const eng::u8 bit = static_cast<eng::u8>(3u - k);
		const eng::u16 r = (c >> (8u + bit)) & 1u;
		const eng::u16 g = (c >> (4u + bit)) & 1u;
		const eng::u16 b = (c >> bit) & 1u;
		out = static_cast<eng::u16>((out << 4) | (r << 3) | (g << 2) | (b << 1) | b);
	}
	return out;
}

/// Entrada `i` de `dualtab`: calor (`val*4`) en la palabra alta + color HAM abajo.
constexpr eng::u32 dual_entry(eng::u16 i) {
	const eng::u16 val = static_cast<eng::u16>((static_cast<eng::u32>(i) * 63u) / 256u);
	return (static_cast<eng::u32>(val) * 4u << 16) | scramble(kPalette[val]);
}

/// Tabla completa (256 entradas), evaluada en compile-time.
struct DualTab {
	eng::u32 v[256];
	constexpr DualTab() : v{} {
		for (eng::u16 i = 0; i < 256u; ++i) {
			v[i] = dual_entry(i);
		}
	}
};

/// Instancia única (inline, sin coste en runtime).
inline constexpr DualTab kDualTab {};

} // namespace fire_rgb
