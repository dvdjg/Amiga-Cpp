#pragma once

/// \file numeric_goap.hpp
/// **Alias del GOAP numerico** sobre la cabecera unica `goap.hpp`. La implementacion vive
/// alli (`eng::ai::Goap<MaxFacts, MaxVars>`); este fichero solo conserva el nombre
/// historico `NumericGoap` y sus constantes, para no romper a sus consumidores.
///
/// Modelo de una accion: booleanos (`require`/`forbid`/`produce`/`consume`) + numericos
/// (`var_ge(v, min)`, `var_le(v, max)` precondiciones; `add(v, delta)`, `set_var(v, nivel)`
/// efectos, saturados a 0..255). Cachés: `plan_cached`, `plan_reusing`, invalidacion
/// selectiva (`invalidate_selective`) y **anytime** (`set_budget`/`partial`). Limites:
/// `MaxFacts <= 32` (hechos) y `MaxVars <= 4` (clave exacta de 64 bits).
///
/// Verificacion: HOST-185, HOST-186 y HOST-316.

#include <eng/ai/planning/goap.hpp>

namespace eng::ai {

/// Techo de variables del dominio numerico (alias historico).
inline constexpr usize numeric_goap_max_vars = goap_max_vars;

/// Numero de hechos que usa el dominio numerico (la clave es `u32` hechos + `u32` niveles).
inline constexpr usize numeric_goap_max_facts = 32u;

/// "Sin asignacion" en `set_var` (alias historico de `detail::var_no_max`).
inline constexpr eng::u8 numeric_goap_no_level = detail::var_no_max;

/// Dominio GOAP **numerico**: 32 hechos + `MaxVars` variables de nivel (0..255).
template <usize MaxVars = goap_max_vars>
using NumericGoap = Goap<32u, MaxVars>;

} // namespace eng::ai
