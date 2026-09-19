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

/// Diagnóstico de compilación del `from_int` de `scalar_traits<Fixed>` (sin cuerpo).
void scalar_from_int_out_of_range();

namespace detail {

/// Diagnóstico de compilación del `scalar_const<Fixed>::from` (sin cuerpo).
void scalar_const_fixed_out_of_range();

} // namespace detail

} // namespace eng::math
