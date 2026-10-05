#pragma once

/// \file sprite_limits.hpp
/// **Límites de hardware de los sprites OCS/ECS (A500)** como constantes de dominio.
///
/// Reúne los topes que condicionan el reparto y el rearmado de sprites, para que el
/// asignador, los drivers y las demos citen un nombre en vez de repetir el número. Cada
/// valor cita su fuente (AHRM 3.ª cap. 4 y las fichas de `docs/reference/amiga/techniques/`).
///
/// Este header es la **fuente única** del número de canales: `SpriteChannelLedger::kChannels`
/// y `SpriteAllocator::kChannels` lo aliasan, de modo que no hay dos `8` sueltos.
///
/// Lógica pura (sin hardware, sin heap).

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Número de canales DMA de sprites del chipset (AHRM 3.ª cap. 4: sprites 0..7).
inline constexpr u8 kSpriteChannels = 8u;

/// Nº máximo de objetos sprite simultáneos en una misma línea: uno por canal.
inline constexpr u8 kSpriteMaxPerLine = kSpriteChannels;

/// Línea(s) de separación mínima entre **dos reusos verticales** del mismo canal (el DMA
/// necesita reencontrar el canal libre; dos sprites contiguos comparten canal porque
/// `bottom` es exclusivo). Fuente: `sprite-techniques-catalog.md` §Multiplexado vertical.
inline constexpr u8 kSpriteVerticalGapLines = 1u;

/// Separación horizontal mínima, en píxeles low-res, entre **dos reusos del mismo canal en
/// la MISMA línea** (multiplexado horizontal por Copper). Por debajo de ~24 px el Copper no
/// llega a reescribir `SPRxPOS` a tiempo: es una carrera contra el haz, no un presupuesto por
/// frame. Fuente: `raster_intent.hpp` (`SpriteHorizontalRearm`) y
/// `sprite-horizontal-multiplex.md`.
inline constexpr u8 kSpriteMinReusePx = 24u;

/// Con **más de** estos bitplanes en pantalla no queda hueco de DMA dentro de la línea para
/// parchear `SPRxDATA`/`DATB` a media línea (patrón Brian the Lion). En 4-5 planos el fetch
/// deja la ventana necesaria. Fuente: `sprite-tricks-games.md` §Brian the Lion.
inline constexpr u8 kSpriteLineDataMaxBitplanes = 5u;

/// Pérdida aproximada de canales altos al activar el scroll horizontal (`BPLCON1`) o cuando
/// el fetch del playfield invade la zona de sprites: el canal que arranca más a la derecha
/// puede no completar su fetch. Reserva de seguridad. Fuente:
/// `sprite-techniques-catalog.md` §Multiplexado horizontal (Copper).
inline constexpr u8 kSpriteHscrollLostChannels = 1u;

} // namespace eng::graphics
