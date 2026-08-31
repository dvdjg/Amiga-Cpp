#pragma once

/// \file ct_array.hpp
/// Array CONSTEXPR genérico construido desde un functor en compile-time.
///
/// Patrón de la 107 (`LissYTable`, `SineTable`): convertir cualquier array
/// constante "a mano" en un GENERADOR. `ct_array<T, N>{ fn }` llama a
/// `fn(i)` para `i in [0,N)` en tiempo de compilación y materializa `T[N]`
/// como datos (sin float ni división runtime si `fn` solo usa constantes o
/// aritmética entera).
///
///   constexpr ct_array<u8, 64> kLissy{ [](usize i){ return (u8)((i*7u/10u)&63u); } };
///   constexpr ct_array<u16, 4> kFetchOff{ [](usize m){ return m==3?48u:(m?16u:0u); } };
///
/// Uso con runtime-index: `k[i]` lee la tabla materializada (sin llamar a `fn`
/// en runtime). Reglas del engine: sin heap, sin RTTI, gnu++23.

#include <eng/core/types.hpp>

namespace eng {

template <typename T, usize N>
struct ct_array {
    T v[N];

    /// Construye la tabla en compile-time llamando a `fn(i)` para i in [0,N).
    constexpr explicit ct_array(auto fn) {
        for (usize i = 0; i < N; ++i) v[i] = static_cast<T>(fn(i));
    }

    constexpr usize size() const { return N; }
    constexpr const T& operator[](usize i) const { return v[i]; }
    constexpr const T* data() const { return v; }
};

} // namespace eng