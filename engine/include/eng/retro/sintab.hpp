#pragma once

/// \file sintab.hpp
/// Tabla de seno **4.12** (4096 pasos = 2π), vocabulario retro. Se genera en compile-time
/// con el generador del núcleo (`eng::SineTable`) y se materializa en `s16` —el formato
/// del original— ocupando **8 KB**.
///
/// El generador (parametrizable y genérico) vive en `eng/core/math/sinetable.hpp`; aquí sólo
/// está la tabla Q concreta, que es lo específico de la especialización retro.

#include <eng/core/data/ct_array.hpp>
#include <eng/core/math/sinetable.hpp>
#include <eng/core/types/types.hpp>

namespace eng::retro {

/// Tabla 4.12 exacta (4096 pasos = 2π) como `s16`: el formato del original.
inline constexpr eng::ct_array<s16, 4096> kSinQ12 {[](eng::usize i) -> s16 {
	return static_cast<s16>(eng::SineTable<4096, 4096>::sample(static_cast<eng::u32>(i)));
}};

/// Alias de puntero para el uso típico (`kSinTab[i]` -> s16 con el valor 4.12).
inline constexpr const s16* kSinTab = kSinQ12.data();

} // namespace eng::retro
