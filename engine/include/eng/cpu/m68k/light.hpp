#pragma once

/// \file light.hpp
/// Especialización para el **procesador 68000** del sombreado. Es del CPU: usa `mulu.w`
/// (vía `arith<s16>`) y extrae el word alto con `swap` en vez de un desplazamiento. El
/// VALOR es idéntico al genérico (HOST-054 lo fija); sólo cambia la emisión.
///
/// `if consteval` mantiene la función `constexpr` (el camino de compilación usa el cuerpo
/// portable; en runtime, la secuencia nativa), igual que en `arith.hpp`.

#include <eng/core/light.hpp>

namespace eng::math {

template <>
struct light_ops<void> {
	template <typename Table>
	[[nodiscard]] static constexpr s16 shade(s32 v, s32 e1_sq, const Table& inv_sqrt) {
		if consteval {
			return shade_portable(v, e1_sq, inv_sqrt);
		} else {
			if (v < 0) {
				v = -v;
			}
			s16 s = hi16(e1_sq);
			if (s > 511) {
				s = 511;
			}
			const s16 vv = hi16(v);
			u32 r = arith<s16>::mulu(static_cast<u16>(vv), inv_sqrt[static_cast<u16>(s)]);
			asm("swap %0" : "+d"(r)); // el word alto pasa a la mitad baja: `>> 16` en 1 instr.
			return static_cast<s16>(r);
		}
	}
};

} // namespace eng::math
