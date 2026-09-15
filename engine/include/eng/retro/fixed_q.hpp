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

#include <eng/core/fixed.hpp>

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

} // namespace eng::retro
