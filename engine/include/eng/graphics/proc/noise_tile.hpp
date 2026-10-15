#pragma once

/// \file noise_tile.hpp
/// **Ruido de valor entero** (`eng::gfx::proc`): campos tileables en punto fijo Q8 usando
/// solo multiplicaciones de 16×16 (`mulu.w`), sumas y desplazamientos — sin `__mulsi3`
/// ni divisores. Es la vía barata para que el 68000 genere campos de mundo (parches,
/// humedad, altura) **en carga**: puente entre la ruta de ruido escalar (`noise.hpp`,
/// MiniFloat16, calidad en host) y el presupuesto de ciclos del 68000 (cada `__mulsi3`
/// cuesta ≈1,1 k ciclos; ver `PROCEDURAL_BITMAPS.md` §Coste).
///
/// Modelo: rejilla de nodos con valor 0..255 del hash, interpolación bilineal con
/// *smoothstep* en Q8 y **periodo en nodos** (`period`, potencia de 2): el campo se repite
/// cada `period << cell_log2` píxeles, así que dos tiles contiguos casan sin costura.
///
/// ```text
///   tile_node(ix, iy, seed) ──► nodo 0..255 (2 mulu.w + xor/shifts)
///   tile_noise2(x, y, …)    ──► bilineal suavizado Q8 (3 mulu.w de interpolación)
///   tile_fbm2(x, y, …)      ──► suma de octavas con pesos 2^(7−k) (shift por octava)
/// ```
///
/// ## Coste
///
/// Por muestra: ~6 `mulu.w` (≈40-70 ciclos en 68000) frente a ≈19,7 k ciclos de
/// `value_noise2<MiniFloat16>` (16 `__mulsi3`). Es la pieza que hace viable generar los
/// campos de bioma de un mapa en la propia máquina.
///
/// ## Estado de verificación: NO VERIFICADA
/// Solo respaldada por test host (`HOST-445`); la demo 119 lo usa en carga (medido).

#include <eng/core/types/types.hpp>

namespace eng::gfx::proc {

/// Multiplica dos `u16` y pliega el producto a 16 bits (`mulu.w` + `xor`): la operación
/// base del hash del ruido, barata en 68000 (sin `__mulsi3`). Para operandos cuyo producto
/// cabe en 16 bits (`a·b ≤ 65535`) el plegado es exacto (la parte alta es 0).
/// \param a,b operandos. \return `(a·b) ^ (a·b >> 16)` truncado a 16 bits.
[[nodiscard]] constexpr eng::u16 mul_fold16(eng::u16 a, eng::u16 b) {
	const eng::u32 p = eng::u32(a) * eng::u32(b);
	return eng::u16(p ^ (p >> 16u));
}

/// Mezcla de avalancha de 16 bits (xor-shift + `mulu.w` ×2): dispersa el nodo por todo el
/// rango. `constexpr` para poder derivar tablas en compilación.
/// \param v valor de entrada. \return valor mezclado.
[[nodiscard]] constexpr eng::u16 mix16(eng::u16 v) {
	v = eng::u16(v ^ (v >> 8u));
	v = mul_fold16(v, 0x88B5u);
	v = eng::u16(v ^ (v >> 7u));
	v = mul_fold16(v, 0x2545u);
	return eng::u16(v ^ (v >> 9u));
}

/// Hash del nodo `(ix, iy)` con semilla → valor 0..255 del campo de valor.
/// \param ix,iy coordenadas del nodo (el llamador las envuelve por el periodo para que el
///              campo sea tileable; ver `tile_noise2`). \param seed semilla del campo.
/// \return valor 0..255.
[[nodiscard]] constexpr eng::u8 tile_node(eng::u16 ix, eng::u16 iy, eng::u16 seed) {
	const eng::u16 h = mix16(eng::u16(ix ^ mix16(eng::u16(iy ^ seed))));
	return eng::u8(h >> 8u);
}

/// División exacta por 255 de un `u16` sin multiplicar ni dividir:
/// `(x + 1 + (x>>8)) >> 8` (identidad clásica para `x ≤ 65535`).
/// \param x dividendo 0..65535. \return `x / 255`.
[[nodiscard]] constexpr eng::u8 div255(eng::u16 x) {
	return eng::u8((eng::u16(x + 1u + (x >> 8u))) >> 8u);
}

/// Tabla de *smoothstep* Q8 `t²(3−2t)` (256 entradas): la construye el **compilador**
/// (`constexpr`, aritmética `u32` en compilación) y en el 68000 es **una lectura de byte**
/// por muestra — sin multiplicaciones, divisores ni aproximaciones (monótona exacta).
struct SmoothstepTable {
	/// Valores suavizados (0→0, 255→255, monótona).
	eng::u8 t[256] {};
	/// Construye la tabla con `(3s²·255 − 2s³ + 32512) / 65025` (redondeo exacto).
	constexpr SmoothstepTable() : t() {
		for (eng::u32 i = 0u; i < 256u; ++i) {
			const eng::u32 s2 = i * i;
			eng::u32 v = (s2 * 765u - s2 * i * 2u + 32512u) / 65025u;
			if (v > 255u) v = 255u;
			t[i] = eng::u8(v);
		}
	}
};

/// Tabla única de suavizado (ver `SmoothstepTable`).
inline constexpr SmoothstepTable kSmoothstep {};

/// *Smoothstep* `t²(3−2t)` en Q8: entrada 0..255 → salida 0..255 (0→0, 255→255, monótona),
/// por **lookup** de `kSmoothstep` (en el 68000: 1 lectura de byte). Evita las facetas del
/// lerp lineal del ruido de valor.
/// \param t peso Q8 (0..255). \return peso suavizado Q8.
[[nodiscard]] constexpr eng::u8 smoothstep_q8(eng::u8 t) { return kSmoothstep.t[t]; }

/// **Ruido de valor tileable** en Q8 (0..255). Rejilla de nodos de `1 << cell_log2`
/// píxeles; el nodo se toma de `tile_node` con coordenadas **envueltas** por `period`
/// (potencia de 2), de modo que el campo es periódico cada `period << cell_log2` píxeles
/// (tileable sin costura). Los pesos de interpolación salen de desplazamientos (el paso
/// de celda es potencia de 2), sin divisiones.
/// \param x,y píxel (los bits bajos eligen la celda). \param cell_log2 log2 del paso de
///        rejilla (2..8). \param period nodos por lado del periodo (potencia de 2, ≥2).
/// \param seed semilla del campo. \return 0..255.
[[nodiscard]] constexpr eng::u8 tile_noise2(eng::u16 x, eng::u16 y, eng::u8 cell_log2,
					    eng::u8 period, eng::u16 seed) {
	if (period < 2u || cell_log2 < 1u || cell_log2 > 8u) return 0u;
	const eng::u8 mask = eng::u8(period - 1u);
	const eng::u16 gx = eng::u16(x >> cell_log2);
	const eng::u16 gy = eng::u16(y >> cell_log2);
	const eng::u8 sub = eng::u8((1u << cell_log2) - 1u);
	const eng::u8 sx = eng::u8(x & sub);
	const eng::u8 sy = eng::u8(y & sub);
	const eng::u8 tx = smoothstep_q8(eng::u8(sx << (8u - cell_log2)));
	const eng::u8 ty = smoothstep_q8(eng::u8(sy << (8u - cell_log2)));
	const eng::u16 ix0 = eng::u16(gx & mask);
	const eng::u16 iy0 = eng::u16(gy & mask);
	const eng::u16 ix1 = eng::u16((gx + 1u) & mask);
	const eng::u16 iy1 = eng::u16((gy + 1u) & mask);
	const eng::u16 v00 = tile_node(ix0, iy0, seed);
	const eng::u16 v10 = tile_node(ix1, iy0, seed);
	const eng::u16 v01 = tile_node(ix0, iy1, seed);
	const eng::u16 v11 = tile_node(ix1, iy1, seed);
	const eng::u16 top = eng::u16(v00 + (((v10 - v00) * tx) >> 8u));
	const eng::u16 bot = eng::u16(v01 + (((v11 - v01) * tx) >> 8u));
	return eng::u8(top + (((bot - top) * ty) >> 8u));
}

/// **fBm de ruido entero**: suma de octavas de `tile_noise2` (cada octava dobla la
/// frecuencia: `cell_log2+k` y periodo ×2 → misma periodicidad física) con pesos
/// `2^(7−k)`, de modo que la salida es la suma desplazada 8 bits (0..255) **sin
/// divisiones**: `Σ 2^(7−k)·n_k ≤ 255·255` cabe en `u16` hasta 8 octavas.
/// \param x,y píxel. \param cell_log2 paso de la octava base. \param period nodos base
///        (potencia de 2). \param octaves nº de octavas (1..8). \param seed semilla.
/// \return 0..255 (la octava base domina; las siguientes añaden detalle).
[[nodiscard]] constexpr eng::u8 tile_fbm2(eng::u16 x, eng::u16 y, eng::u8 cell_log2,
					  eng::u8 period, eng::u8 octaves, eng::u16 seed) {
	eng::u16 acc = 0u;
	eng::u8 cl = cell_log2;
	eng::u8 per = period;
	for (eng::u8 o = 0u; o < octaves && o < 8u && cl <= 8u; ++o) {
		const eng::u16 n = tile_noise2(x, y, cl, per, eng::u16(seed + o * 0x9E37u));
		acc = eng::u16(acc + eng::u16(n << (7u - o)));
		if (cl < 8u) cl = eng::u8(cl + 1u);
		if (per < 0x80u) per = eng::u8(per << 1u);
	}
	return eng::u8(acc >> 8u);
}

} // namespace eng::gfx::proc
