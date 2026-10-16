#pragma once

/// \file scalar_fwd.hpp
/// **Declaraciones (primarias) de los puntos de extensión de escalar**, en un header neutro
/// de bajo nivel. Permite que cada escalar concreto especialice estos traits en **su propia
/// cabecera** (`fixed.hpp`, `minifloat.hpp`, …) sin que las cabeceras genéricas
/// (`linalg.hpp`, `numeric_traits.hpp`, `scalar_math.hpp`, `noise.hpp`) conozcan ninguna
/// representación concreta.
///
/// Regla de oro del diseño: una cabecera es lo más genérica posible; sólo las de una
/// implementación concreta (el propio escalar, `retro/`, `platform/`, `cpu/`) fijan tipos.

#include <eng/core/types/types.hpp>

namespace eng::math {

template <typename S>
struct scalar_traits;
template <typename S>
struct scalar_div;
template <typename S>
struct scalar_const;
template <typename S>
struct numeric_traits;
template <typename S>
struct noise_traits;

/// **Conversión decimal de texto** del escalar, sin coma flotante en runtime: el escalar
/// declara su valor como entero escalado por `10^k` (`scaled`) y la cuantización inversa
/// al ulp de su representación (`from_scaled`).
///
///   scalar_decimal<Fixed<s16,12>>: scaled(x, 2) = round(x·100) ; from_scaled(150, 2) = 1.5
///
/// La consume `eng/core/util/text.hpp` (`to_chars(out, x, frac_digits)` /
/// `parse(text, x, frac_digits`) para que el formateo/parseo decimal de un escalar
/// fraccionario no incluya su cabecera ni asuma su formato. `k` va de 0 a
/// `kDecimalDigitsMax`; una especialización que no pueda cumplirlo en la máquina objetivo
/// puede bloquear la instanciación con `static_assert` (patrón `scalar_div<Fixed<s32,E>>`).
template <typename S>
struct scalar_decimal;

/// Máximo de decimales del contrato de `scalar_decimal<S>` (leer/escribir un escalar con
/// punto decimal). Con 4 decimales basta para HUD/config y garantiza que toda la aritmética
/// de las especializaciones cabe en 32 bits para representaciones `s16` (p. ej. `x·10^4`
/// de un 4.12: `32768·10000 = 3.3·10^8 < 2^31`).
inline constexpr eng::u8 kDecimalDigitsMax = 4u;

// Funciones matematicas como punto de extension (las definen `scalar_math.hpp` y, por
// escalar, `minifloat_math.hpp`/`fixed_math.hpp`).
template <typename S>
struct scalar_sqrt;
template <typename S>
struct scalar_sin;
template <typename S>
struct scalar_cos;
template <typename S>
struct scalar_sincos;
template <typename S>
struct scalar_exp2;
template <typename S>
struct scalar_log2;
template <typename S>
struct scalar_log;
template <typename S>
struct scalar_exp;
template <typename S>
struct scalar_pow;
template <typename S>
struct scalar_tan;
template <typename S>
struct scalar_atan2;
template <typename S>
struct scalar_asin;
template <typename S>
struct scalar_acos;

/// Política de `float`/`double`: son tipos **aritméticos del lenguaje**, no una
/// representación que elija el motor, así que sus traits (el default de
/// `numeric_traits`/`scalar_traits` y los `scalar_*<float|double>` de `scalar_math.hpp`)
/// viven en las cabeceras genéricas. Solo las representaciones con formato propio
/// (`Fixed`, `MiniFloat16`) se especializan en **su** cabecera (`fixed*.hpp`,
/// `minifloat*.hpp`).

/// Diagnóstico de compilación del `from_int` de `scalar_traits<Fixed>` (sin cuerpo).
void scalar_from_int_out_of_range();

namespace detail {

/// Diagnóstico de compilación del `scalar_const<Fixed>::from` (sin cuerpo).
void scalar_const_fixed_out_of_range();

} // namespace detail

} // namespace eng::math
