#pragma once

/// \file plane_layout.hpp
/// **Disposición de los bitplanes** en memoria: un único enum del engine. `Contiguous`
/// (planos uno tras otro; alias `Planar`) o `Interleaved` (filas de planos alternadas).
///
/// Unifica los antiguos `SceneLayout` (`composition`) y `BobLayout` (objetos), que eran el
/// mismo concepto con dos nombres; ambos quedan como alias de `PlaneLayout`. El
/// `PlaneLayout` de `eng::gfx` (capa de memoria de `Bitmap`) describe el mismo concepto en la
/// capa baja. Ver `ROADMAP_API_COHERENCE.md` (F1).

#include <eng/core/types/types.hpp>

namespace eng::graphics {

enum class PlaneLayout : eng::u8 {
	Contiguous = 0,      ///< planos uno tras otro (addr = plano·plano_bytes + …)
	Planar = Contiguous, ///< alias de `Contiguous` (vocabulario de objetos/BOB)
	Separate = Contiguous, ///< alias de `Contiguous` (vocabulario de `eng::gfx::Bitmap`)
	Interleaved = 1,     ///< filas de planos alternadas (un solo recorrido intercalado)
};

} // namespace eng::graphics
