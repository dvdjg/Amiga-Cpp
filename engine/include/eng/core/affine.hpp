#pragma once

/// \file affine.hpp
/// Proyección de un vértice crudo con una transformación afín. Es un **punto de
/// personalización**: el genérico de aquí es portable (un producto por multiplicación) y
/// un backend de CPU puede especializarlo para empaquetar dos productos en una sola
/// instrucción (el 68000 lo hace con `muls.w`).
///
/// Convención de escalas (la del original, idéntica en genérico y backend): la fila se
/// pliega con `>> 4` y un término de traslación preescalado (`e`), y el resultado son los
/// **numeradores** de la perspectiva; `div16(xp, zp)` los lleva a pantalla.
///
/// Algebraicamente `(c0+y)·(c1+x) − x·y = c0·c1 + c0·x + c1·y`, así que el genérico
/// evalúa esos productos uno a uno y el 68000 los mete dos a dos en un registro. El
/// VALOR es el mismo; sólo cambia el número de multiplicaciones.

#include <eng/core/arith.hpp>
#include <eng/core/fixed.hpp>
#include <eng/core/linalg.hpp>
#include <eng/core/types.hpp>

namespace eng::math {

/// Los tres productos `c0·c1 + c0·x + c1·y` de una fila. **Punto de personalización**:
/// el genérico los hace uno a uno; un backend de CPU puede meter dos en un solo registro
/// (el 68000, con `muls.w`). Algebraicamente es `(c0+y)·(c1+x) − x·y`.
template <typename SR, typename SL>
struct pack3_ops {
	[[nodiscard]] static s32 eval(SR c0, SR c1, SL x, SL y) {
		using R = typename SR::repr;
		return arith<R>::mul(c0.v, c1.v) + arith<R>::mul(c0.v, x.v) + arith<R>::mul(c1.v, y.v);
	}
};

/// Numeradores de la proyección de un vértice (antes del `div16`).
struct Projected3 {
	s32 xp;
	s32 yp;
	s32 zp;
};

/// Trait de proyección, especializado por tipo de afín (y, en el backend, por CPU).
template <typename Aff>
struct projector;

template <int N, typename SR, typename SL>
struct projector<Affine<N, SR, SL>> {
	using affine_t = Affine<N, SR, SL>;
	using ratio_t = SR;
	using length_t = SL;

	/// Lo precalculable UNA vez por matriz: los términos de traslación plegados.
	struct cache {
		const affine_t& a;
		s32 e0; // fila x: `(t.x − normfx(m00·m01)) << 8`
		s32 e1; // fila y
		s16 tz; // fila z: la traslación va sin plegar (la fila z normaliza)
	};

	[[nodiscard]] static cache make(const affine_t& a) {
		return cache {
			a,
			static_cast<s32>((static_cast<s32>(a.t.x().v) - dot(a.m.m[0][0], a.m.m[0][1]).v) << 8),
			static_cast<s32>((static_cast<s32>(a.t.y().v) - dot(a.m.m[1][0], a.m.m[1][1]).v) << 8),
			static_cast<s16>(a.t.z().v - dot(a.m.m[2][0], a.m.m[2][1]).v)};
	}

	/// Producto escalar entero de una fila: los tres primeros productos van por el punto
	/// de personalización (`pack3_ops`) y el término `c2·z` aparte.
	[[nodiscard]] static s32 row(ratio_t c0, ratio_t c1, ratio_t c2, length_t x, length_t y,
				     length_t z) {
		return pack3_ops<SR, SL>::eval(c0, c1, x, y) + arith<typename SR::repr>::mul(c2.v, z.v);
	}

	[[nodiscard]] static Projected3 project(const cache& c, s16 x, s16 y, s16 z) {
		const length_t px {x}, py {y}, pz {z};
		const ratio_t* m0 = c.a.m.m[0];
		const ratio_t* m1 = c.a.m.m[1];
		const ratio_t* m2 = c.a.m.m[2];
		return Projected3 {(row(m0[0], m0[1], m0[2], px, py, pz) >> 4) + c.e0,
				   (row(m1[0], m1[1], m1[2], px, py, pz) >> 4) + c.e1,
				   // La fila z normaliza el 8.24 a 4.12 (un `>> 12`), como el `normfx` del
				   // original; el estrechado a 16 bits forma parte de la convención.
				   static_cast<s16>(row(m2[0], m2[1], m2[2], px, py, pz) >> 12) + c.tz};
	}
};

} // namespace eng::math

// Seleccion del backend de CPU (mismo patron que arith.hpp).
#if defined(__mc68000__)
#include <eng/cpu/m68k/affine.hpp>
#endif
