#pragma once

/// \file bitmap_buffer.hpp
/// **Buffer de índices de color (chunky) de geometría fija en compilación** (`eng::gfx::proc`):
/// el lienzo sobre el que trabajan los generadores procedurales (`fill`, evaluadores de
/// píxel, texturas y el pintor de terreno). 1 byte por píxel = índice en la paleta
/// compartida del asset; el volcado a bitplanes Amiga lo hace `eng::graphics::c2p`
/// (`c2p.hpp`), que ya existe y **no** se duplica aquí.
///
/// ```text
///   BitmapBuffer<W,H>          índice 0..255 por píxel (chunky, CPU)
///        │  fill / evaluadores / texturas / pintor de terreno   (generación)
///        ▼
///   graphics::c2p(...)         → planos (Chip) para BPLxPT/Blitter (carga)
/// ```
///
/// ## Coste
///
/// Todo el trabajo de esta librería es de **generación** (carga/setup): los bucles por
/// píxel pueden pagar `mulu.w`/`divu.w`, pero **nunca** se ejecutan por frame; el camino
/// de frame solo consume el buffer ya generado. Presupuesto y método:
/// `docs/guides/optimization/METODO_OPTIMIZACION.md`.
///
/// ## Estado de verificación: VERIFICADA (demo 119)
/// Ejercitada en hardware por `demos/techniques/amiga/effects/119_proc_world` (frames +
/// composición del mundo); tests host `HOST-442`/`HOST-443`.
/// Diseño y uso de la librería completa: `docs/engine/architecture/PROCEDURAL_BITMAPS.md`.

#include <eng/core/types/types.hpp>

namespace eng::gfx::proc {

/// Buffer de índices de `W`×`H` píxeles (1 byte por píxel, cero-inicializado).
/// El índice 0 se reserva por convenio como transparente/fondo (ver `TileSpec` y el
/// pintor de terreno); el resto son colores de la paleta del atlas.
/// \tparam W ancho en píxeles. \tparam H alto en filas.
template <eng::u16 W, eng::u16 H>
struct BitmapBuffer {
	/// Índices de color fila a fila (`pixels[y * W + x]`); nace a cero.
	eng::u8 pixels[W * H] {};
	/// Ancho del buffer en píxeles (constante de compilación, la usa `fill` y `c2p`).
	static constexpr eng::u16 width = W;
	/// Alto del buffer en filas (constante de compilación).
	static constexpr eng::u16 height = H;

	/// Píxel `(x, y)` mutable. \param x columna 0..W-1. \param y fila 0..H-1.
	/// Sin comprobación de rango: la geometría es constante y la validan los bucles `fill`.
	/// \return referencia al índice de color (0..255).
	[[nodiscard]] constexpr eng::u8& at(eng::u16 x, eng::u16 y) { return pixels[y * W + x]; }
	/// Píxel `(x, y)` de solo lectura (mismo contrato que la versión mutable).
	[[nodiscard]] constexpr eng::u8 at(eng::u16 x, eng::u16 y) const { return pixels[y * W + x]; }
};

} // namespace eng::gfx::proc
