#pragma once

/// \file sinetable.hpp
/// Tabla de seno Q8 generada en COMPILE-TIME (patrón `LissYTable` de la 107).
///
/// `SineTable<Amp, Steps>` calcula en tiempo de compilación (constexpr, sin libm
/// ni float en runtime) las `Steps` muestras de `Amp * sin(2π·i/Steps)` para
/// `i` en [0, Steps). Cambiar la amplitud es solo instanciar otro template
/// parameter; no hay datos escritos a mano ni arrays copiados.
///
/// Precisión: aproximación racional de Bhaskara I sobre [0, π] (máx. error
/// relativo ~0,4 %; exacta en 0 y π/2), reducida al cuadrante y con signo por
/// la simetría del seno. Suficiente para 8 bits (desviación ≤ 1 en la última
/// cifra frente a `round(Amp·sin)`).
///
/// Reglas del engine: sin heap, sin RTTI, gnu++23. El float vive SOLO en la
/// evaluación constexpr del compilador; la tabla emitida es entera.

#include <eng/core/ct_array.hpp>
#include <eng/core/types.hpp>

namespace eng {

template <s32 Amp, u32 Steps = 64>
struct SineTable {
    /// Tabla materializada por `ct_array` (el functor solo se evalúa en
    /// compile-time; `k[i]` con i runtime lee la tabla, sin float por frame).
    ct_array<s32, Steps> v {
        [](usize i) -> s32 { return SineTable::sample(static_cast<u32>(i)); }
    };

    /// Muestra `i & (Steps-1)` (Steps potencia de dos).
    constexpr s32 operator[](u8 i) const { return v[i & (Steps - 1u)]; }

    /// Valor de la muestra `i` (i < Steps): `Amp * sin(2π i / Steps)` (Q8).
    static constexpr s32 sample(u32 i) {
        const double tau = 6.283185307179586;
        const double pi = 3.141592653589793;
        // Ángulo en [0, 2π) → seno por simetría hacia [0, π] + signo.
        const double rad = tau * static_cast<double>(i) / static_cast<double>(Steps);
        double s = 1.0;
        double x = rad;
        if (x > pi) { x = tau - x; s = -1.0; }
        // Bhaskara I: sin(x) ≈ 16·x·(π−x) / (5π² − 4·x·(π−x)), exacto en 0 y π/2.
        const double xp = x * (pi - x);
        const double den = 5.0 * pi * pi - 4.0 * xp;
        const double sinv = 16.0 * xp / den;
        const double val = (s * sinv) * static_cast<double>(Amp);
        const double rounded = val + (val < 0.0 ? -0.5 : 0.5);
        return static_cast<s32>(rounded);
    }
};

} // namespace eng