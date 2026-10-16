#pragma once

/// \file mode_switch.hpp
/// Zona de conmutación de **geometría de vídeo** a mitad de frame.
///
/// Un `ModeSwitchZone` describe un tramo vertical (p. ej. un HUD inferior) que se
/// muestra con distinto número de planos, otra ventana de fetch (`DDF`) y otros
/// módulos (`BPLxMOD`) que el campo principal. La composición los emite en orden
/// ascendente de `top`; el `Scheduler` los materializa en un único `WAIT` de raster
/// reprogramando, en este orden canónico:
///
///   BPLCON0 (BPU + bits de modo) -> DDFSTRT/DDFSTOP -> BPL1MOD/BPL2MOD
///     -> BPLxPT (par alto/bajo por plano) -> [BPLCON3] -> [BPLCON1] -> [paleta]
///
/// El orden importa: el `DDF` y los módulos deben quedar reprogramados **antes** de
/// los punteros para que la línea del corte empiece a leer el tramo nuevo con su
/// geometría, y los registros de modo que no son geometría (`BPLCON3`/`BPLCON1`)
/// van **después** de los punteros: intercalarlos entre `BPLCON0` y los punteros
/// hace que el DMA pierda el último plano del tramo (verificado en WinUAE-DBG con
/// 4/5 planos; ver `OPTIMIZACION_GPP_68000.md`). Es el invariante MI09 de
/// `docs/reference/amiga/hardware/amiga-hardware-invariants-microtests.md`.
///
/// El modo EHB **no** lo selecciona `BPLCON3`: sale de `BPLCON0` con 6 planos y HAM=0
/// (AHRM 3.ª cap. 3, "Color selection in Extra Half Brite mode", y `drawing.cpp:2741`);
/// lo que existe en `BPLCON3` es el bit 9 `KILLEHB` (AHRM Appendix C) y el bit 0
/// `ENBPLCN3` (habilita las funciones ECS). Un tramo sin EHB se consigue bajando los
/// planos en `BPLCON0` (lo que ya hace la zona) o con `KILLEHB`.
///
/// La variante que **no** cambia geometría (sólo reapunta planos/paleta manteniendo
/// `BPLCON0`/`DDF`/`BPLxMOD` del campo) es `CopperIntentKind::BitplaneSplit`; ambas
/// conviven: `BitplaneSplit` es la opción conservadora y `ModeSwitchZone` la que
/// habilita tramos de distinto número de planos.
///
/// ```text
///   frame (campo principal)                       tramo del ModeSwitchZone (p. ej. HUD)
///   ───────────────────────                       ──────────────────────────────────
///   …                                             WAIT al raster de `top`, y en ORDEN canónico:
///   [ corte en `top` ] ────────────────────────►   BPLCON0(BPU+modo) → DDFSTRT/DDFSTOP → BPL1MOD/BPL2MOD
///   …                                               → BPLxPT (alto/bajo por plano) → [BPLCON3] → [BPLCON1] → [paleta]
///   El DDF/mods van ANTES de los punteros; BPLCON3/BPLCON1 DESPUÉS (intercalarlos pierde el último plano).
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Tramo con geometría de vídeo propia.
///
/// `bitplanes` es la base del **plano 0** del tramo (mismo convenio que
/// `PlayfieldHardwareView::real_base`) y `plane_bytes` el stride entre planos: el
/// scheduler reapunta `planes` punteros consecutivos. `planes = 0` deja sólo la
/// reprogramación de `BPLCON0`/`DDF`/módulos (útil para desactivar planos).
struct ModeSwitchZone {
	u16 top = 0;                  // línea raster del corte (inicio del tramo)
	u16 bplcon0 = 0;              // BPU + bits de modo (HIRES/EHB/DBLPF...) del tramo
	bool set_bplcon3 = false;     // true = emitir BPLCON3 (bit 0 ENBPLCN3, bit 9 KILLEHB)
	u16 bplcon3 = 0;
	bool set_bplcon1 = false;     // true = emitir BPLCON1 (p. ej. 0 para anular el fino)
	u16 bplcon1 = 0;
	u16 ddfstrt = 0;
	u16 ddfstop = 0;
	u16 bpl1mod = 0;
	u16 bpl2mod = 0;
	u8  planes = 0;               // cuántos BPLxPT reapuntar (0..6)
	u32 plane_bytes = 0;          // stride entre planos del tramo (bytes)
	eng::ChipPlaneView bitplanes {}; // base del plano 0 del tramo (Chip, solo lectura)
	/// Paleta opcional del tramo (HUD con sus propios colores). `palette_colors`
	/// marca cuántos COLORxx emitir; 0 = no emitir ninguno.
	eng::PaletteWords palette {};
	u8 palette_colors = 0;

	/// El `DDF` debe caer en un límite de fetch (múltiplo de 4). Sin esto la ventana
	/// del tramo se desplaza media palabra y los planos se desalinean.
	[[nodiscard]] constexpr bool ddf_aligned() const noexcept {
		return (ddfstrt & 0x0003u) == 0u && (ddfstop & 0x0003u) == 0u;
	}
};

} // namespace eng::graphics
