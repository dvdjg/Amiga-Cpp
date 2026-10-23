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
/// necesita reencontrar el canal libre; `bottom` exclusivo no basta: dos tramos contiguos
/// dejan cero líneas). La exigen `SpriteAllocator` y `SpriteDmaChain`. Fuente: AHRM 3.ª
/// cap. 4 («at least one horizontal line between each reuse of a sprite processor»).
inline constexpr u8 kSpriteVerticalGapLines = 1u;

/// Altura máxima de un sprite (líneas): cota de recorte que aplican `SpriteConfig` /
/// `HwSpritePlacement` (p. ej. `SpriteManager::apply` y `arm_object`). Fuente: AHRM 3.ª
/// cap. 4 (la ventana `VSTART`..`VSTOP` del sprite).
inline constexpr u16 kSpriteMaxHeightLines = 128u;

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

/// Cabecera **OFF** de una estructura DMA de sprite (`VSTART=VSTOP=254`, HSTART=0): un canal
/// que la lee queda desarmado y no se auto-arma con la posición real (antipatrón *Phantom
/// Sprite*, `docs/reference/amiga/techniques/sprite-layer.md` §10). Es la cabecera que llevan
/// las estructuras cocinadas para armado por Copper (`SPRxPOS`/`SPRxCTL` se escriben aparte).
inline constexpr u16 kSpriteOffPos = 0xfe00u;
inline constexpr u16 kSpriteOffCtl = 0xfe00u;

/// Origen X de pantalla en el registro: el sprite aparece en `x_pantalla = HSTART − 0x81`
/// (`HSTART = (SPRxPOS & 0xff)*2 + (SPRxCTL & 1)`; fuente: `../WinUAE-DBG/drawing.cpp:2706`,
/// con el HSTART de la ventana estándar `DIWSTRT=0x81`). Para posicionar por **x de pantalla**
/// el valor del registro es `x + kSpriteScreenXOrigin`.
inline constexpr u16 kSpriteScreenXOrigin = 0x81u;

/// X mínima de pantalla (px low-res) para **parchear `SPRxPOS` a media línea**: la escritura
/// del Copper debe caer **después del fetch de sprites de la línea** (la ventana de sprites
/// se cierra en el arranque del secuenciador de bitplanes, `../WinUAE-DBG/custom.cpp:10111`,
/// `hp <= plfstrt_sprite`); con `DDFSTRT=$38` la primera escritura segura está en
/// `DDFSTRT/2 ≈ 28` px. Es la X del primer tramo de un rearmado por línea
/// (`SpriteLineLayer`, patrón Parasol Stars).
inline constexpr u16 kSpriteLineMinX = 28u;

/// HSTART mínimo del primer tramo de un rearmado por línea (la X mínima de pantalla pasada
/// al registro): `kSpriteScreenXOrigin + kSpriteLineMinX`. Lo valida
/// `setup::check_sprite_line_hpos`.
inline constexpr u16 kSpriteLineMinHstart = kSpriteScreenXOrigin + kSpriteLineMinX;

} // namespace eng::graphics
