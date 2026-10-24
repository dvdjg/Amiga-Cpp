#pragma once

/// \file wire_edge.hpp
/// Aristas de alambre: una arista topológica de la malla y una arista ya proyectada
/// a pantalla. Son tipos de datos puros, compartidos por el renderer genérico y por
/// el rasterizador de líneas.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Arista única de una malla, referenciada por índices de vértice.
struct WireEdge {
	u16 a = 0;
	u16 b = 0;
};

/// Arista ya transformada y proyectada a píxeles de pantalla.
struct WireScreenEdge {
	s16 x0 = 0;
	s16 y0 = 0;
	s16 x1 = 0;
	s16 y1 = 0;
	u8 color = 0; ///< máscara de planos / índice de color.
};

} // namespace eng::graphics