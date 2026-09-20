#pragma once

/// \file blitter_state.hpp
/// **Valores preparados por el backend** para trabajos de Blitter que se encolan o se
/// repiten: el frame de un BOB OR por desplazamiento (`OrBob`), los parámetros
/// precalculados de una línea EOR (`LineEor`) y el estado del C2P 4bpp por fases (`C2p4`).
///
/// Viven en `eng::graphics` (dominio) y no en el backend concreto para que la lógica de
/// demo no nombre `MinimalBackend::C2p4State`/`LineEorParams`/`OrBobEntry`. El backend
/// Amiga los **aliasa** (`using C2p4State = eng::graphics::C2p4;`) y rellena/consume sus
/// campos; las operaciones (`blitter_or_bobs`, `blitter_line_eor_prepare`/`draw`,
/// `c2p_4bpp_step`) siguen siendo suyas. Los campos reflejan lo que el backend
/// precalcula (p. ej. registros `BLTCONx`), de modo que el layout es estable y el
/// llamador no recompone la aritmética cada frame.

#include <eng/core/types.hpp>

namespace eng::graphics {

/// Entrada de un lote de **BOBs OR por desplazamiento** (`D = A | D`): origen (frame del
/// atlas), destino (plano 0 de la scanline) y desplazamiento fino X (0..15).
struct OrBob {
	const void* source = nullptr;
	void* dest = nullptr;
	u8 shift = 0;
};

/// Parámetros **precalculados** de una línea EOR (ONEDOT) para el Blitter: los registros
/// y el error inicial de Bresenham, de forma que dibujar la línea sea solo programar.
/// Los rellena `MinimalBackend::blitter_line_eor_prepare` y los consume
/// `blitter_line_eor_draw`; el llamador puede reutilizarlos en varios planos sin
/// recalcular.
struct LineEor {
	u16 bltcon0 = 0;    ///< BLTCON0 (minterm/desplazamiento de la línea)
	u16 bltcon1 = 0;    ///< BLTCON1 (modo línea, signos)
	u16 bltamod = 0;    ///< BLTAMOD (módulo de A)
	u16 bltbmod = 0;    ///< BLTBMOD (módulo de B)
	u16 bltsize = 0;    ///< BLTSIZE (alto/ancho del blit)
	s16 derr = 0;       ///< error inicial del algoritmo de Bresenham (BLTAPT)
	u32 row_offset = 0; ///< desplazamiento de la línea dentro del plano
};

/// Estado del **C2P 4bpp por fases** (13 fases): fase actual, buffer chunky (su segunda
/// mitad es el staging planar) y punteros a los 4 bitplanes destino.
struct C2p4 {
	u8 phase = 0;         ///< fase actual del C2P (0..12)
	u8* chunky = nullptr; ///< buffer chunky (su 2ª mitad es el destino planar)
	u8* planes[4] = {nullptr, nullptr, nullptr, nullptr}; ///< punteros a los 4 bitplanes
	u16 bytes = 0;        ///< `BLTSIZE` del original (p. ej. 10240)
};

} // namespace eng::graphics
