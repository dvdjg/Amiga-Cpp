#pragma once

/// \file isqrt.hpp
/// Raíz cuadrada entera (isqrt) — port directo de `libmisc/fx.c` de
/// `demoscene-repo-orig`.
///
/// El original (bien depurado) calcula `floor(sqrt(n))` con dos tablas
/// (`_sqrt[65]` de semillas y `_nlz[256]` de conteo de ceros iniciales), sin
/// división ni punto flotante. Se importa tal cual, solo con los tipos del
/// engine (`eng::u32`) y en namespace `eng`; la lógica no se reescribe.
///
/// Uso:
///   u32 r = eng::isqrt(100);   // 10
///   u32 s = eng::isqrt(32768); // 181

#include <eng/core/types.hpp>

namespace eng {

namespace detail {

/// Tabla de semillas del original (índice = nº de bits de `n`, valor = 2^16
/// veces la raíz aproximada). No tocar: la mantiene intacta el port.
constexpr u16 kSqrt[65] = {
    0, 1, 1, 2, 2, 4, 5, 8, 11, 16, 22, 32, 45, 64, 90, 128, 181, 256, 362, 512,
    724, 1024, 1448, 2048, 2896, 4096, 5792, 8192, 11585, 16384, 23170, 32768, 46340,
    32768, 33276, 33776, 34269, 34755, 35235, 35708, 36174, 36635, 37090, 37540, 37984,
    38423, 38858, 39287, 39712, 40132, 40548, 40960, 41367, 41771, 42170, 42566, 42959,
    43347, 43733, 44115, 44493, 44869, 45241, 45611, 45977,
};

/// Conteo de ceros iniciales por byte bajo (del original).
constexpr u8 kNLZ[256] = {
    0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
};

/// Conteo de ceros iniciales de `x` (del original).
constexpr u16 nlz(u32 x) {
    u32 y;
    u16 n = 0;
    if ((y = x >> 16u) != 0u) { n += 16u; x = y; }
    if ((y = x >> 8u) != 0u) { n += 8u; x = y; }
    return n + kNLZ[x];
}

} // namespace detail

/// Parte entera de la raíz cuadrada de `n` (n >= 0). Port fiel del original.
constexpr u32 isqrt(u32 n) {
    const u16 cnt = detail::nlz(n);
    u32 t;
    if (cnt <= 6u) {
        t = n << (6u - cnt);
    } else {
        t = n >> (cnt - 6u);
    }
    t &= 31u;
    t += 33u;
    t = (static_cast<u32>(detail::kSqrt[cnt]) * static_cast<u32>(detail::kSqrt[t])) << 1u;
    return t >> 16u;
}

} // namespace eng