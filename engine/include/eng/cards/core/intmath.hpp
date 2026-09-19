#pragma once

/// \file intmath.hpp
/// Reexporta la aritmética sin libcalls de `eng/core/util/intmath.hpp` bajo
/// `eng::cards` y añade los helpers específicos de póker. El núcleo vive en
/// `eng::util` para que la capa de persona de `eng::sim` lo comparta sin duplicarlo.
///
/// Verificación: HOST-191 y codegen-report (sin libgcc en 68000).

#include <eng/core/types.hpp>
#include <eng/core/util/intmath.hpp>

namespace eng::cards {

using eng::util::div32;
using eng::util::divmod32;
using eng::util::divu_w;
using eng::util::mul_1000;
using eng::util::permille_u32;

/// Equity multiway aproximada a partir del equity **heads-up** en por mil: modela
/// `P(ganar vs n) ≈ hu / (hu + (1-hu)·n)`. Se usa con la tabla preflop (que es HU).
[[nodiscard]] constexpr u16 multiway_from_heads_up(u16 hu_permille, u8 opponents) noexcept {
	if (opponents == 0u) {
		return 1000u;
	}
	const u32 win = eng::math::mulu16(hu_permille, 1000u);
	const u32 lose = 1000u - hu_permille;
	const u32 denom = static_cast<u32>(hu_permille) +
	                  eng::math::mulu16(static_cast<u16>(lose), static_cast<u16>(opponents));
	if (denom == 0u) {
		return 0u;
	}
	return static_cast<u16>(div32(win, denom));
}

} // namespace eng::cards
