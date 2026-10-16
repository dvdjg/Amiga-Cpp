#pragma once

/// \file hud_text.hpp
/// **Formateo mínimo para el HUD de depuración**: números decimales sin `/` ni `%` — en m68k esas
/// operaciones sobre 32 bits son libcalls (`__udivsi3`/`__umodsi3`, ~150 ciclos) y el HUD se
/// dibuja en el bucle de juego (regla de coste, `OPTIMIZACION_GPP_68000.md` §Disciplina). El
/// decimal se construye por **resta** contra una tabla de potencias de 10 (peor caso ~50 ciclos).
///
/// Uso típico (telemetría de una franja o del overlay del depurador):
/// ```cpp
/// char num[6];
/// eng::debug::dec_u16(count, num);
/// ```

#include <eng/core/types/types.hpp>

namespace eng::debug {

/// Escribe `v` en decimal en `out` (capacidad ≥ 6) y devuelve la longitud (sin terminador).
/// \param v    valor 0..65535.
/// \param out  buffer destino (se escribe `\0` al final).
/// \return nº de dígitos escritos.
inline eng::u8 dec_u16(eng::u16 v, char* out) noexcept {
	constexpr eng::u16 kPow10[5] = {10000u, 1000u, 100u, 10u, 1u};
	eng::u8 n = 0u;
	bool lead = true; // dígitos líderes a cero se omiten (salvo el "0" de un valor cero)
	for (eng::u8 i = 0u; i < 5u; ++i) {
		eng::u8 d = 0u;
		while (v >= kPow10[i]) {
			v = static_cast<eng::u16>(v - kPow10[i]);
			++d;
		}
		if (d != 0u || !lead || kPow10[i] == 1u) {
			out[n++] = static_cast<char>('0' + d);
			lead = false;
		}
	}
	out[n] = '\0';
	return n;
}

} // namespace eng::debug
