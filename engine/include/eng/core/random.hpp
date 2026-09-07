#pragma once

/// \file random.hpp
/// PRNG xoroshiro64++ — port de `libc/stdlib/random.c` de `demoscene-repo-orig`.
///
/// El repo origen implementa xoroshiro64++ (rotaciones + xor + suma, sin
/// multiplicación) con un `rol` escrito por rangos y `swap16` para aprovechar
/// el `swap` del 68000. Esa variante es ARITMÉTICAMENTE EQUIVALENTE a la
/// rotación de 32 bits "ingenua" del algoritmo publicado (verificado: produce
/// la misma secuencia), así que aquí se expone la forma estándar y limpia
/// `rotl32`, con este comentario como referencia al truco 68000.
///
/// Portabilidad host/cruz. `eng::u32` = `unsigned long` es de 32 bits en el
/// cruce m68k pero de 64 en el host GCC x86. La aritmética de bits del PRNG
/// debe operar SIEMPRE en 32 bits exactos, así que internamente se usa
/// `__UINT32_TYPE__` (uint32_t de GNU, freestanding) y el resultado se
/// devuelve como `eng::u32`.
///
/// Uso:
///   eng::Xoroshiro64pp rng{s0, s1};
///   u32 v = rng.next();      // valor 32 bits
///   u32 d = rng.next_mod(100); // 0..99

#include <eng/core/types.hpp>

namespace eng {

namespace detail {

using u32c = __UINT32_TYPE__; // uint32_t GNU freestanding (32 bits host y m68k)

/// Rotación a la izquierda de 32 bits por `k` (k en [1,31]).
constexpr u32c rotl32(u32c x, u32c k) { return (x << k) | (x >> (32u - k)); }

} // namespace detail

/// xoroshiro64++ (sin estado global, 2×u32 de estado).
class Xoroshiro64pp {
public:
    constexpr explicit Xoroshiro64pp(u32 s0, u32 s1)
        : m_s0(static_cast<detail::u32c>(s0)), m_s1(static_cast<detail::u32c>(s1)) {}

    constexpr u32 next() {
        detail::u32c x0 = m_s0;
        detail::u32c x1 = m_s1;
        x1 ^= x0;
        x0 = detail::rotl32(x0, 26u) ^ x1 ^ (x1 << 9u);
        x1 = detail::rotl32(x1, 13u);
        m_s0 = x0;
        m_s1 = x1;
        return static_cast<u32>(detail::rotl32(x0 + x1, 17u) + x0);
    }

    constexpr u32 next_mod(u32 n) { return next() % n; }

private:
    detail::u32c m_s0;
    detail::u32c m_s1;
};

} // namespace eng