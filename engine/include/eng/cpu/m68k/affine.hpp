#pragma once

/// \file affine.hpp
/// Especialización para el **procesador 68000** del empaquetado de la proyección. Es del
/// CPU (sirve para Amiga, Atari ST o Mega Drive), no del chipset.
///
/// `(c0+y)·(c1+x) − x·y = c0·c1 + c0·x + c1·y`, y el producto de 32 bits de los dos
/// factores empaquetados lleva los dos productos dentro. Así los tres productos de la
/// fila salen con **dos `muls.w`** en vez de tres. `xy` es el producto `x·y` del vértice,
/// que el llamador comparte entre las tres filas (como el `MULVERTEX` del original).
/// El VALOR es idéntico al genérico (comprobado por HOST-053).

#include <eng/core/fixed_affine.hpp>
#include <eng/retro/fixed_q.hpp>

namespace eng::math {

template <>
struct pack3_ops<eng::retro::q12, eng::retro::q0> {
	/// Con `xy` ya calculado, aquí queda **1 `muls.w`** (el otro es el `c2·z` de la fila).
	[[nodiscard]] static s32 eval(eng::retro::q12 c0, eng::retro::q12 c1, eng::retro::q0 x,
				      eng::retro::q0 y, s32 xy) {
		const s16 t0 = static_cast<s16>(c0.v + y.v); // empaquetado (ver cabecera)
		const s16 t1 = static_cast<s16>(c1.v + x.v);
		return arith<s16>::mul(t0, t1) - xy;
	}
};

} // namespace eng::math
