#pragma once

/// \file box.hpp
/// **Rectángulo 2D de 16 bits** (`eng::Box`) para UI, recortes, dirty rects y geometría de
/// pantalla. Es el tipo único del engine para "rectángulo de píxeles": los tipos que ya
/// existían con semánticas distintas (`field::SurfaceRect` con `s32`, `field::ClipRect` y
/// `graphics::DirtyRect` en formato `left/top/right/bottom`, `eng::retro::Rect` en fixed)
/// se convierten a/desde `Box` en su propia capa, sin duplicar la lógica de `contains`,
/// `inset` o `intersect`.
///
/// Convención: `w`/`h` son tamaños (no bordes) y `contains` es **inclusivo** en `right()` y
/// `bottom()` (`x + w - 1`, `y + h - 1`), que es como se comportan las primitivas de dibujo
/// de `field::Surface`. Un `Box` con `w == 0` o `h == 0` está vacío.

#include <eng/core/types.hpp>

namespace eng {

/// Rectángulo de píxeles (origen + tamaño), 16 bits: barato en el 68000.
struct Box {
	s16 x = 0;
	s16 y = 0;
	u16 w = 0;
	u16 h = 0;

	[[nodiscard]] constexpr bool empty() const { return w == 0u || h == 0u; }
	[[nodiscard]] constexpr s16 right() const { return static_cast<s16>(x + w - 1); }
	[[nodiscard]] constexpr s16 bottom() const { return static_cast<s16>(y + h - 1); }

	/// `true` si `(px, py)` cae dentro del rectángulo (bordes inclusivos).
	[[nodiscard]] constexpr bool contains(s16 px, s16 py) const {
		return w != 0u && h != 0u && px >= x && py >= y && px <= right() && py <= bottom();
	}

	/// Rectángulo encogido `n` píxeles por cada lado (vacío si no cabe).
	[[nodiscard]] constexpr Box inset(u8 n) const {
		const u16 d = static_cast<u16>(2u * n);
		return { static_cast<s16>(x + n), static_cast<s16>(y + n),
			 static_cast<u16>(w > d ? w - d : 0u),
			 static_cast<u16>(h > d ? h - d : 0u) };
	}

	/// Construye desde bordes inclusivos `left/top/right/bottom` (formato `ClipRect`/`DirtyRect`).
	[[nodiscard]] static constexpr Box from_ltrb(s16 l, s16 t, s16 r, s16 b) {
		return (r < l || b < t) ? Box {}
					: Box { l, t, static_cast<u16>(r - l + 1),
						static_cast<u16>(b - t + 1) };
	}
};

/// `true` si los dos rectángulos comparten al menos un píxel.
[[nodiscard]] constexpr bool overlaps(const Box& a, const Box& b) {
	return !a.empty() && !b.empty() && a.x <= b.right() && b.x <= a.right() &&
	       a.y <= b.bottom() && b.y <= a.bottom();
}

/// Intersección (vacía si no se tocan).
[[nodiscard]] constexpr Box intersect(const Box& a, const Box& b) {
	if (!overlaps(a, b)) return {};
	const s16 l = a.x > b.x ? a.x : b.x;
	const s16 t = a.y > b.y ? a.y : b.y;
	const s16 r = a.right() < b.right() ? a.right() : b.right();
	const s16 bo = a.bottom() < b.bottom() ? a.bottom() : b.bottom();
	return Box::from_ltrb(l, t, r, bo);
}

/// Rectángulo mínimo que cubre ambos.
[[nodiscard]] constexpr Box merge(const Box& a, const Box& b) {
	if (a.empty()) return b;
	if (b.empty()) return a;
	const s16 l = a.x < b.x ? a.x : b.x;
	const s16 t = a.y < b.y ? a.y : b.y;
	const s16 r = a.right() > b.right() ? a.right() : b.right();
	const s16 bo = a.bottom() > b.bottom() ? a.bottom() : b.bottom();
	return Box::from_ltrb(l, t, r, bo);
}

/// Traslación.
[[nodiscard]] constexpr Box translate(const Box& b, s16 dx, s16 dy) {
	return { static_cast<s16>(b.x + dx), static_cast<s16>(b.y + dy), b.w, b.h };
}

} // namespace eng
