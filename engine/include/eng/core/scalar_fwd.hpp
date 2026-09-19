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
