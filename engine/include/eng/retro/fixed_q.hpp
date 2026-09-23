#pragma once

/// \file fixed_q.hpp
/// Vocabulario fixed-point **retro** (formato Q): los escalares 4.12, entero y 8.24 con
/// los que están escritos los efectos y las rutinas del demoscene.
///
/// Es una **especialización** de `eng::math::Fixed<Repr, Exp, Policy>`, no núcleo. El
/// núcleo es genérico (sirve igual a `float` o a un escalar complejo de usuario) y no
/// conoce la convención Q: `q12 = Fixed<s16, 12>` es una decisión retro (palabra de 16
/// bits, 4 bits de fracción, atada a `SinTableQ12` y al layout del original). Tenerla en
/// `eng::math` haría que el núcleo hablara en Q12.
///
/// Quien escribe un efecto usa estos alias; quien necesite el genérico escribe
/// `Fixed<Repr, Exp, Policy>`.

#include <eng/core/math/fixed.hpp>

namespace eng::retro {

/// 4.12 (RATIO): parte lineal de una transformación, senos/cosenos.
using q12 = eng::math::Fixed<s16, 12>;
/// Entero (LONGITUD): coordenadas de objeto/mundo.
using q0 = eng::math::Fixed<s16, 0>;
/// 8.24: acumulador exacto de productos de 4.12.
using q24 = eng::math::Fixed<s32, 24>;

/// 4.12 con redondeo al más cercano.
using q12_round = eng::math::Fixed<s16, 12, eng::math::RoundPolicy>;
/// Mismo layout que `q12`, pero saturando al estrechar.
using q12_sat = eng::math::Fixed<s16, 12, eng::math::SaturatePolicy>;

// --- Vocabulario crudo (el formato como `s16`/`s32`, sin tipar) ---------------
// Los ports 1:1 de lib2d/lib3d trabajan con el formato implícito en la convención
// (`normfx`, shifts). La forma tipada es `Fixed<Repr, Exp>`; esto es el vocabulario
// crudo de esos ports.

/// 4.12 crudo (`s16`): `1.0 == 4096`.
using fix = s16;
/// 8.8 crudo (`s16`): `1.0 == 256`.
using fix88 = s16;
/// 8.24 crudo (`s32`): `1.0 == 1 << 24`.
using fix8_24 = s32;

/// 1.0 en 4.12.
constexpr fix kOne = 4096;
/// 1.0 en 8.8.
constexpr fix88 kOne88 = 256;
/// 0.5 en 8.8 (redondeo en el recorte de línea).
constexpr fix88 kHalf88 = 128;
/// 1.0 en 8.24 (numerador de los recíprocos de escala).
constexpr fix8_24 kOne8_24 = 1 << 24;
/// Fracción de 8.8 (bits a bajar para normalizar).
constexpr s32 kShift88 = 8;

/// Entero a 4.12.
constexpr fix fx12i(int i) { return static_cast<fix>(i * 4096); }

/// Normaliza un producto 8.24 a 4.12 (shift aritmético de 12): el `normfx` del
/// original (`lsll #4; swap` sobre el 68000).
constexpr fix normfx(s32 a) { return static_cast<fix>(a >> 12); }

} // namespace eng::retro
