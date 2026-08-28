#pragma once

/// \file ehb_tile_scroll.hpp
/// Compatibilidad del primer driver EHB con el driver generico de scroll.
///
/// El scroll por tiles ya no es exclusivo de EHB: el driver real vive en
/// `tile_scroll.hpp` (`TileScrollScene<Mode>`) y se compila para single playfield
/// de 4/5/6 bitplanes y dual playfield 2+3/3+3. Este archivo mantiene los tipos
/// `Ehb*` que usaban las primeras demos como alias de la instancia EHB (single 6).

#include <eng/graphics/drivers/tile_scroll.hpp>

namespace eng::graphics::drivers {

/// Escena de scroll EHB: single playfield de 6 bitplanes.
using EhbTileScrollScene = TileScrollScene<TileScrollMode::ehb()>;

/// Configuracion generica de una escena de scroll.
using EhbTileScrollConfig = TileScrollConfig;

/// Planificador horizontal (compatibilidad).
using EhbHorizontalRingPrefetch = HorizontalRingPrefetch<20, 30>;

/// Planificador bidireccional 20x16 visible / 30x26 superficie (compatibilidad).
using EhbBidirectionalRingPrefetch = BidirectionalRingPrefetch<20, 16, 30, 26>;

/// Trabajo de prefetch 2D (compatibilidad).
using EhbBidirectionalPrefetchJob = BidirectionalPrefetchJob;

} // namespace eng::graphics::drivers
