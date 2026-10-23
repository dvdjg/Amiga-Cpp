#pragma once

/// \file face_style.hpp
/// **Estilo de una cara 3D**: índice de paleta + patrón de cobertura + flags.
/// Es el tipo que transporta el material plano entre el asset (`ModelMaterial`),
/// la escena y el relleno (`Surface::fill_polygon`). No conoce paletas RGB ni
/// iluminación: el color es un índice y el patrón un id del catálogo
/// `eng::graphics::FillPattern` (0 = sólido).
///
/// Sin heap, sin RTTI y sin hardware.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Estilo plano de una cara.
struct FaceStyle {
	u8 color = 0; ///< índice de paleta (0..255).
	u8 pattern = 0; ///< id de `FillPattern` (0 = `Solid`); el relleno valida el rango.
	u16 flags = 0; ///< reservado: `opaque`/`doubleSided`/`noCull` (G3).
};

} // namespace eng::graphics
