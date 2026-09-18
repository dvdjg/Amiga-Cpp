#pragma once

/// \file color.hpp
/// **Color RGB444** de Amiga (`eng::util`): empaquetado, interpolación, escalado,
/// conversión desde HSV y operaciones de **paleta completa**, todo en entero y sin
/// `float`.
///
/// El chipset Amiga guarda el color como 12 bits `0x0RGB` (un nibble por canal). Estas
/// utilidades operan sobre ese `u16` directamente y usan la aritmética de palabra del
/// 68000 (`muls.w`/`divs.w`, `word.hpp`), sin `float` ni divisiones que acaben en
/// libgcc. Sirven para degradados de paleta, fundidos, parpadeos y efectos de color.
///
/// Uso:
///   const eng::u16 cielo = eng::util::rgb444(1, 3, 8);
///   const eng::u16 medio = eng::util::lerp444(cielo, blanco, 1u, 2u);   // a mitad
///   const eng::u16 rojo  = eng::util::hsv_to_rgb444(0u, 255u, 255u);
///   eng::util::palette_lerp(dst, a, b, paso, total);                    // transición
///   eng::util::palette_scale(dst, src, 15u, 16u);                      // fundido

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/word.hpp>

namespace eng::util {

/// Empaqueta tres componentes (0..15) en un color RGB444.
[[nodiscard]] constexpr u16 rgb444(u16 r, u16 g, u16 b) noexcept {
	return static_cast<u16>(((r & 0xfu) << 8u) | ((g & 0xfu) << 4u) | (b & 0xfu));
}

[[nodiscard]] constexpr u16 rgb444_r(u16 c) noexcept { return static_cast<u16>((c >> 8u) & 0xfu); }
[[nodiscard]] constexpr u16 rgb444_g(u16 c) noexcept { return static_cast<u16>((c >> 4u) & 0xfu); }
[[nodiscard]] constexpr u16 rgb444_b(u16 c) noexcept { return static_cast<u16>(c & 0xfu); }

/// Interpola canal a canal: `a` en `num==0`, `b` en `num==den`. `num`/`den` deben caber
/// en `s16`. Con `den == 0` devuelve `a`.
[[nodiscard]] constexpr u16 lerp444(u16 a, u16 b, u16 num, u16 den) noexcept {
	if (den == 0u) {
		return a;
	}
	u16 out = 0u;
	for (u8 shift = 0u; shift < 12u; shift += 4u) {
		const s16 ca = static_cast<s16>((a >> shift) & 0xfu);
		const s16 cb = static_cast<s16>((b >> shift) & 0xfu);
		const s32 diff = eng::math::mul16(static_cast<s16>(cb - ca), static_cast<s16>(num));
		const s16 step = eng::math::div16(diff, static_cast<s16>(den));
		const s16 v = static_cast<s16>(ca + step);
		out = static_cast<u16>(out | (static_cast<u16>(v & 0xf) << shift));
	}
	return out;
}

/// Escala cada canal por `num/den` (0..den); acota a 15. Con `den==0` devuelve `c`.
[[nodiscard]] constexpr u16 scale444(u16 c, u16 num, u16 den) noexcept {
	if (den == 0u) {
		return c;
	}
	u16 out = 0u;
	for (u8 shift = 0u; shift < 12u; shift += 4u) {
		const s16 comp = static_cast<s16>((c >> shift) & 0xfu);
		const s32 prod = eng::math::mul16(comp, static_cast<s16>(num));
		s16 v = eng::math::div16(prod, static_cast<s16>(den));
		if (v > 15) {
			v = 15;
		}
		out = static_cast<u16>(out | (static_cast<u16>(v & 0xf) << shift));
	}
	return out;
}

/// Conversión HSV → RGB444. `hue` en `[0, 1536)` (6 sectores × 256), `sat`/`val` en
/// `[0, 255]`. Aproxima la división por 255 con `>> 8` (error < 1/255 por paso): basta
/// para paletas y efectos, y evita cualquier libcall de división.
[[nodiscard]] constexpr u16 hsv_to_rgb444(u16 hue, u16 sat, u16 val) noexcept {
	const u16 sector = static_cast<u16>((hue >> 8u) % 6u);
	const u16 f = static_cast<u16>(hue & 0xffu);
	const u16 s = sat > 255u ? 255u : sat;
	const u16 v = val > 255u ? 255u : val;

	const u16 p = static_cast<u16>((v * (255u - s)) >> 8u);
	const u32 sf = (static_cast<u32>(s) * f) >> 8u;
	const u16 q = static_cast<u16>((v * (255u - sf)) >> 8u);
	const u32 s_inv = (static_cast<u32>(s) * (255u - f)) >> 8u;
	const u16 t = static_cast<u16>((v * (255u - s_inv)) >> 8u);

	u16 r = 0u;
	u16 g = 0u;
	u16 b = 0u;
	switch (sector) {
		case 0u: r = v; g = t; b = p; break;
		case 1u: r = q; g = v; b = p; break;
		case 2u: r = p; g = v; b = t; break;
		case 3u: r = p; g = q; b = v; break;
		case 4u: r = t; g = p; b = v; break;
		default: r = v; g = p; b = q; break;
	}
	return rgb444(static_cast<u16>(r >> 4u), static_cast<u16>(g >> 4u),
		      static_cast<u16>(b >> 4u));
}

/// **Transición de paleta**: `dst[i] = lerp444(a[i], b[i], num, den)` hasta el menor de
/// los tres tamaños; devuelve cuántos colores escribió. `num` va de 0 (todo `a`) a `den`
/// (todo `b`). Es el núcleo de `ColorTransition` de `libgfx` (`palette.h`) sin estado ni
/// registros: el llamante decide qué paleta física recibe el resultado.
[[nodiscard]] constexpr usize palette_lerp(Span<u16> dst, Span<const u16> a,
					   Span<const u16> b, u16 num, u16 den) noexcept {
	usize n = dst.size();
	if (a.size() < n) {
		n = a.size();
	}
	if (b.size() < n) {
		n = b.size();
	}
	for (usize i = 0; i < n; ++i) {
		dst[i] = lerp444(a[i], b[i], num, den);
	}
	return n;
}

/// **Fundido de paleta**: `dst[i] = scale444(src[i], num, den)` hasta el menor tamaño;
/// devuelve cuántos colores escribió. `num == 0` deja la paleta en negro (fundido a
/// negro, `FadeBlack` de `libgfx`); `num == den` la deja igual. `dst` y `src` pueden ser
/// la misma memoria (fundido *in place*).
[[nodiscard]] constexpr usize palette_scale(Span<u16> dst, Span<const u16> src, u16 num,
					    u16 den) noexcept {
	usize n = dst.size();
	if (src.size() < n) {
		n = src.size();
	}
	for (usize i = 0; i < n; ++i) {
		dst[i] = scale444(src[i], num, den);
	}
	return n;
}

} // namespace eng::util
