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

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/util.hpp>

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

    /// Valor en `[0, n)`. Sin división: potencia de dos por máscara y el resto por
    /// **rechazo** con máscara (evita `__umodsi3` en el 68000). `n == 0` devuelve 0.
    constexpr u32 next_mod(u32 n) {
        if (n == 0u) {
            return 0u;
        }
        if ((n & (n - 1u)) == 0u) {
            return next() & (n - 1u);
        }
        u32 mask = n - 1u;
        mask |= mask >> 1u;
        mask |= mask >> 2u;
        mask |= mask >> 4u;
        mask |= mask >> 8u;
        mask |= mask >> 16u;
        u32 r = next() & mask;
        while (r >= n) {
            r = next() & mask;
        }
        return r;
    }

private:
    detail::u32c m_s0;
    detail::u32c m_s1;
};

/// Entero uniforme en `[lo, hi)` (por módulo; sesgo despreciable para juegos).
constexpr u32 next_range(Xoroshiro64pp& rng, u32 lo, u32 hi) {
    if (hi <= lo) {
        return lo;
    }
    return lo + rng.next_mod(hi - lo);
}

/// Entero con signo uniforme en `[-range, +range]`.
constexpr s32 next_symmetric(Xoroshiro64pp& rng, u32 range) {
    if (range == 0u) {
        return 0;
    }
    const u32 span = range * 2u + 1u;
    return static_cast<s32>(rng.next_mod(span)) - static_cast<s32>(range);
}

/// ¿Acierta con probabilidad `num/den`? (`num>=den` siempre; `den==0` nunca).
constexpr bool chance(Xoroshiro64pp& rng, u32 num, u32 den) {
    if (den == 0u || num == 0u) {
        return false;
    }
    if (num >= den) {
        return true;
    }
    return rng.next_mod(den) < num;
}

/// Elige un elemento al azar (precondición: vista no vacía).
template <class T>
[[nodiscard]] const T& pick(Xoroshiro64pp& rng, Span<const T> items) {
    return items[rng.next_mod(static_cast<u32>(items.size()))];
}

/// Baraja la vista in situ (Fisher-Yates).
template <class T>
void shuffle(Xoroshiro64pp& rng, Span<T> items) {
    for (usize i = items.size(); i > 1u; --i) {
        const u32 j = rng.next_mod(static_cast<u32>(i));
        if (j != i - 1u) {
            util::swap(items[i - 1u], items[j]);
        }
    }
}

/// Aproximación **entera** a una gaussiana (suma de 4 uniformes, campana) centrada en
/// `mean` y con dispersión ~`stddev`. Sin `float` ni libm.
constexpr s32 next_gaussian_approx(Xoroshiro64pp& rng, s32 mean, u32 stddev) {
    if (stddev == 0u) {
        return mean;
    }
    const u32 span = stddev * 2u;
    s32 sum = 0;
    for (u8 i = 0; i < 4u; ++i) {
        sum += static_cast<s32>(rng.next_mod(span));
    }
    // Suma en [0, 8·sd) con media 4·sd; se centra en `mean`.
    return mean + (sum - static_cast<s32>(4u * stddev));
}

} // namespace eng