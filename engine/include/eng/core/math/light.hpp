#pragma once

/// \file light.hpp
/// Sombreado por cara (color 0..15) del culling de `lib3d`. Es un **punto de
/// personalización**: el núcleo aporta la versión portable (que sólo usa `arith<R>`) y
/// una CPU puede especializar `light_ops`, igual que `arith` y `pack3_ops`.
///
/// Fórmula (port de `UpdateFaceVisibility`): con `v = normal·(cam − vértice)` en escala
/// cruda y `e1_sq = |cam − vértice|²`, el color es
/// `hi16( hi16(|v|) · inv_sqrt[clamp(hi16(e1_sq), 0, 511)] )`, **sin `sqrt` en runtime**
/// (`inv_sqrt` es la tabla `65535/sqrt(x)` en formato 0.16).

#include <eng/core/math/arith.hpp>
#include <eng/core/types/types.hpp>

namespace eng::math {

/// Parte alta de un entero de 32 bits como `s16` (el `swap16` del original).
[[nodiscard]] constexpr s16 hi16(s32 x) {
	return static_cast<s16>(static_cast<u32>(x) >> 16);
}

/// Cuerpo portable del sombreado; lo reutiliza una especialización de CPU en `consteval`.
template <typename Table>
[[nodiscard]] constexpr s16 shade_portable(s32 v, s32 e1_sq, const Table& inv_sqrt) {
	if (v < 0) {
		v = -v;
	}
	s16 s = hi16(e1_sq);
	if (s > 511) {
		s = 511;
	}
	const s16 vv = hi16(v);
	return static_cast<s16>(
		arith<s16>::mulu(static_cast<u16>(vv), inv_sqrt[static_cast<u16>(s)]) >> 16);
}

/// Rasgo del sombreado. La versión por defecto es portable; cada CPU la especializa.
template <typename R = void>
struct light_ops {
	template <typename Table>
	[[nodiscard]] static constexpr s16 shade(s32 v, s32 e1_sq, const Table& inv_sqrt) {
		return shade_portable(v, e1_sq, inv_sqrt);
	}
};

} // namespace eng::math

// Selección del backend por CPU objetivo (no por máquina), como en `arith.hpp`.
#if defined(__mc68000__)
#include <eng/cpu/m68k/light.hpp>
#endif
