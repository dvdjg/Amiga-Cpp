#pragma once

/// \file fast_div.hpp
/// División/módulo enteros que se optimizan SOLO cuando el denominador es una
/// constante conocida en tiempo de compilación (NTTP); en el peor de los casos
/// delegan en la división nativa completa (runtime).
///
/// Mecanismo C++23: template con non-type parameter (el denominador N llega
/// como constante), `if constexpr` para ramificar entre "potencia de dos" y
/// "caso general", y `ct_ilog2` calculado con `consteval`.
///
///   - N potencia de dos (16, 32, 8…):  cociente = v >> log2(N), resto = v & (N-1).
///     El compilador emite 1-2 instrucciones (sin __udivsi3).
///   - N constante pero no potencia de dos (320, 288…): v / N y v % N se reducen
///     a la multiplicación por la constante mágica (libdivide-like), sin llamada.
///   - N desconocido (runtime): división nativa completa — el peor caso, y el
///     único que paga __udivsi3, solo si el caller no conoce el denominador.
///
/// Uso típico en el engine (sin heap, sin RTTI, gnu++23): el `ScrollEngine`
/// recibe su geometría como NTTP `ScrollConsts` (constantes a priori) y usa
/// `fast_div<>`; si una dimensión es 0, la obtiene runtime del sink (fallback).
///
/// Regla de rendimiento (AGENTS): el CPU se limita a decidir cambios sin pagar
/// divisiones cada frame; las tallas de tile/display, conocidas en compile-time,
/// deben entrar como constantes y no como `u16` runtime.

#include <eng/core/types.hpp>

namespace eng {

/// log2 entero de `n` (n > 0). `consteval` → se calcula en tiempo de compilación.
consteval u32 ct_ilog2(u32 n) {
    u32 e = 0;
    while (n > 1u) { n >>= 1u; ++e; }
    return e;
}

/// ¿`n` es potencia de dos? `consteval`.
consteval bool ct_is_pow2(u32 n) { return n != 0u && (n & (n - 1u)) == 0u; }

/// Version RUNTIME de `ct_is_pow2` (denominador que no llega como NTTP).
/// En 68000 no hay multiplicacion 32x32, asi que la division/módulo por una
/// constante NO potencia de dos acaba en `__udivsi3`/`__umodsi3` (libcall, ~150
/// ciclos) incluso a `-Os`; por una POTENCIA de dos es un `lsr`/`and` (1-2
/// ciclos). Cuando una geometria es potencia de dos "casi siempre" pero llega
/// runtime (config), detectarlo y usar shift/mask evita el libcall.
constexpr bool is_pow2(u32 n) { return n != 0u && (n & (n - 1u)) == 0u; }

/// `log2` entero RUNTIME (solo valido si `n` es potencia de dos).
constexpr u32 ilog2(u32 n) { u32 e = 0; while (n > 1u) { n >>= 1u; ++e; } return e; }

/// Cociente `v / (1<<shift)` con signo, redondeando hacia abajo (floor). El
/// desplazamiento aritmetico de `s32` es floor por definicion en C++20.
constexpr s32 asr_floor(s32 v, u32 shift) { return v >> shift; }

/// División/módulo por un denominador CONSTANTE `N` (NTTP).
///
///   q = fast_div<N>::q(v)    cociente  v / N
///   r = fast_div<N>::r(v)    resto     v % N
///   fast_div<N>::qr(v, q, r) ambos en una única llamada (1 div en m68k)
///
/// `v` debe ser sin signo (u32): el m68k traduce `& (N-1)` de u32 directamente;
/// para `s32` no-negativos basta `static_cast<u32>(v)` (el corkscrew usa que la
/// semántica de `%` sea la de floor, y todos los valores son >= 0).
template <u32 N>
struct fast_div {
    static_assert(N != 0u, "fast_div: denominador N = 0");
    static constexpr bool is_pow2 = ct_is_pow2(N);
    static constexpr u32 exp      = ct_ilog2(N); // shift válido solo si is_pow2

    static constexpr u32 q(u32 v) {
        if constexpr (is_pow2) return v >> exp;
        return v / N;
    }
    static constexpr u32 r(u32 v) {
        if constexpr (is_pow2) return v & (N - 1u);
        return v % N;
    }
    static constexpr void qr(u32 v, u32& q_, u32& r_) {
        if constexpr (is_pow2) { q_ = v >> exp; r_ = v & (N - 1u); }
        else if constexpr (true) { q_ = v / N; r_ = v % N; }
    }
};

/// División/módulo runtime de una sola pasada: cociente y resto de `v / n`.
/// En m68k ambos salen de un único `divu.w`/`divuw.l`; evita llamar dos veces a
/// `__udivsi3` cuando se necesitan ambos (aunque el denominador sea runtime).
struct runtime_div {
    static u32 q(u32 v, u32 n) {
        u32 q_ = v / n;
        return q_;
    }
    static u32 r(u32 v, u32 n) {
        u32 r_ = v % n;
        return r_;
    }
    static void qr(u32 v, u32 n, u32& q_, u32& r_) {
        q_ = v / n;
        r_ = v - q_ * n; // un único divu; el resto sale de la resta (sin %)
    }
};

} // namespace eng