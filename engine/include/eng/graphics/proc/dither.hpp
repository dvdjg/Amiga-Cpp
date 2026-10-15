#pragma once

/// \file dither.hpp
/// **Dithering ordenado (matrices de Bayer)** y cuantización a `levels` índices de color
/// (`eng::gfx::proc`). Es el puente entre una intensidad continua (0..255, p. ej. la
/// salida de `fbm2`) y un índice de paleta discreto, sin interpolar color en RGB.
///
/// Se usa en dos sitios: al cuantizar texturas (`to_index`, `fill_texture`) y al fundir
/// dos materiales por peso (`MaterialLayer`) o un borde con el interior (`blend_index`).
/// El umbral es **espacial y determinista**, así que dos tiles con la misma rampa no
/// introducen costura (a diferencia de un dither temporal o aleatorio).
///
/// ```text
///   intensidad 0..255 ──quantize_dither──► índice 0..levels-1
///                │              (umbral Bayer::threshold8: 0..251 por celda 8×8)
///                └──quantize_plain──► índice redondeado (sin ruido)
/// ```
///
/// ## Coste
///
/// Por píxel son unas pocas instrucciones (lookup de matriz + `mulu.w` de 16 bits), apto
/// para generación y para mezclas puntuales; nunca se ejecuta por frame en el engine.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-442`); sin demo que la ejercite en hardware.

#include <eng/core/types/types.hpp>

namespace eng::gfx::proc {

/// Matrices de dithering ordenado de Bayer 2×2, 4×4 y 8×8 (valores 0..n²-1). Son tablas
/// `constexpr`: su única presencia en runtime es el lookup por celda.
struct Bayer {
	/// Matriz 2×2 (valores 0..3); suficiente para `levels` ≤ 4 o patrones gruesos.
	static constexpr eng::u8 b2[2][2] = {
		{ 0, 2 },
		{ 3, 1 },
	};

	/// Matriz 4×4 (valores 0..15); dither de 16 niveles.
	static constexpr eng::u8 b4[4][4] = {
		{  0,  8,  2, 10 },
		{ 12,  4, 14,  6 },
		{  3, 11,  1,  9 },
		{ 15,  7, 13,  5 },
	};

	/// Matriz 8×8 (valores 0..63); la usan `threshold8` y todas las mezclas del engine.
	static constexpr eng::u8 b8[8][8] = {
		{  0, 32,  8, 40,  2, 34, 10, 42 },
		{ 48, 16, 56, 24, 50, 18, 58, 26 },
		{ 12, 44,  4, 36, 14, 46,  6, 38 },
		{ 60, 28, 52, 20, 62, 30, 54, 22 },
		{  3, 35, 11, 43,  1, 33,  9, 41 },
		{ 51, 19, 59, 27, 49, 17, 57, 25 },
		{ 15, 47,  7, 39, 13, 45,  5, 37 },
		{ 63, 31, 55, 23, 61, 29, 53, 21 },
	};

	/// Umbral de la 8×8 para la celda `(x, y)`, reescalado a ~[0, 255] (`v·255/64`, hecho
	/// con `>> 6`: shift, sin división). El máximo es 251 (< 255) a propósito: así el
	/// extremo «siempre opaco» de una mezcla sigue ganando contra cualquier umbral.
	/// \param x,y  coordenadas de píxel (se enmascaran a 3 bits cada una).
	/// \return umbral 0..251 (menor = la celda exige más peso para cambiar).
	[[nodiscard]] static constexpr eng::u8 threshold8(eng::u16 x, eng::u16 y) {
		return (b8[y & 7u][x & 7u] * 255u) >> 6u;
	}
};

/// Cuantiza una intensidad 0..255 a `levels` niveles **sin dither**, redondeando al más
/// cercano: `(I·(L−1) + 127) >> 8`. El `+127` redondea y garantiza los extremos
/// (`0 → 0`, `255 → L−1`), lo que el naive `(I·(L−1)) >> 8` no cumple.
/// \param intensity  intensidad 0..255 (p. ej. un canal de color o ruido cuantizado).
/// \param levels     niveles de salida (2..32; `<= 1` devuelve 0).
/// \return índice 0..levels-1.
[[nodiscard]] constexpr eng::u8 quantize_plain(eng::u8 intensity, eng::u8 levels) {
	if (levels <= 1u) return 0u;
	const eng::u16 v = intensity * (levels - 1u) + 127u;
	return v >> 8u;
}

/// Cuantiza con **dither ordenado** Bayer: añade el umbral de la celda escalado a niveles
/// (`thr·(L−1)>>8`) antes del redondeo, de modo que un píxel con intensidad intermedia
/// oscila ±1 según la celda 8×8. Rango garantizado `[0, L−1]` y extremos estables
/// (el término de dither es ≤ 30 para `L ≤ 32`: `251·31>>8`).
/// \param intensity  intensidad 0..255. \param x,y  píxel (módulo 8 en el umbral).
/// \param levels     niveles de salida (2..32; `<= 1` devuelve 0).
/// \return índice 0..levels-1 (matriz de Bayer espacial: sin costura entre tiles).
[[nodiscard]] constexpr eng::u8 quantize_dither(eng::u8 intensity, eng::u16 x, eng::u16 y,
						eng::u8 levels) {
	if (levels <= 1u) return 0u;
	const eng::u16 v =
		intensity * (levels - 1u) + ((Bayer::threshold8(x, y) * (levels - 1u)) >> 8u) + 127u;
	return v >> 8u;
}

} // namespace eng::gfx::proc
