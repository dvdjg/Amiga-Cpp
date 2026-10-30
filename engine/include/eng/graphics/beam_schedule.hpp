#pragma once

/// \file beam_schedule.hpp
/// Decisión de seguridad de un **grupo atómico de BOBs** frente al haz del raster.
///
/// El Blitter escribe el mismo bitmap que Denise lee línea a línea. Un grupo
/// (restore del fondo + save del fondo nuevo + draw del objeto) solo se puede
/// lanzar si:
/// - el haz ya pasó su caja (`line >= bottom + margen`), o
/// - el Blitter termina antes de que el haz llegue a `top` (`line + coste < top`).
///
/// Si no se cumple ninguna, el grupo se **difiere entero**: se conserva el arte
/// anterior completo, nunca una mezcla de dos frames. La estimación de coste usa
/// palabras de Blitter y una cota conservadora de palabras por línea; el margen
/// cubre el arranque y la incertidumbre de `VPOSR`. Ajustar con medidas, no con
/// el caso medio.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Cota conservadora de palabras que el Blitter completa en una línea PAL.
/// Un Blitter OCS procesa una palabra cada 8 ciclos de CPU y una línea PAL dura
/// ~454 ciclos; 48 deja margen para display DMA y Copper. No usar 56-57: es el
/// límite teórico, no una cota segura.
inline constexpr u32 kBeamWordsPerLine = 3u; // medido en 5bpp 320: el display deja ~3 palabras/linea al Blitter
/// Líneas de margen por arranque del Blitter, contención de bus y latencia de VPOSR.
inline constexpr u32 kBeamMarginLines = 4u;
/// Última línea visible de un campo PAL; la caja nunca se espera más abajo.
inline constexpr u32 kBeamLastLine = 309u;

enum class BeamWriteDecision : u8 {
	Now,   ///< se puede escribir ya (el haz pasó o el blit llega antes)
	Wait,  ///< el haz está dentro de la caja; esperar o diferir
	Defer, ///< no llega antes del deadline; diferir al siguiente frame
};

/// Decisión para un grupo con caja vertical `[top, bottom)` en líneas de pantalla.
/// \param line          línea actual de `VPOSR` (0..311)
/// \param display_start línea absoluta del `DIWSTRT` (p. ej. 44 en PAL)
/// \param top           borde superior de la caja, relativo a la pantalla
/// \param bottom        borde inferior exclusivo, relativo a la pantalla
/// \param words         palabras totales del grupo (todos sus jobs/planos)
[[nodiscard]] constexpr BeamWriteDecision beam_group_decision(
	u16 line, u16 display_start, u16 top, u16 bottom, u32 words) noexcept {
	const u32 top_abs = static_cast<u32>(display_start) + top;
	u32 bottom_abs = static_cast<u32>(display_start) + bottom;
	if (bottom_abs > kBeamLastLine) bottom_abs = kBeamLastLine;
	const u32 blit_lines = (words + kBeamWordsPerLine - 1u) / kBeamWordsPerLine + 2u;
	const u32 l = line;
	if (l >= bottom_abs + kBeamMarginLines) return BeamWriteDecision::Now;
	if (l + blit_lines + kBeamMarginLines < top_abs) return BeamWriteDecision::Now;
	if (l < top_abs) return BeamWriteDecision::Defer;
	return BeamWriteDecision::Wait;
}

/// Línea segura a partir de la cual reintentar un grupo diferido.
[[nodiscard]] constexpr u16 beam_retry_line(u16 display_start, u16 bottom) noexcept {
	const u32 line = static_cast<u32>(display_start) + bottom + kBeamMarginLines;
	return static_cast<u16>(line > kBeamLastLine ? kBeamLastLine : line);
}

} // namespace eng::graphics
