#pragma once

/// \file display.hpp
/// **Escena declarativa de bajo nivel** (`eng::scene`): emite el display estándar (DMACON,
/// BPLCON0, módulos, DIW/DDF y punteros `BPLxPT`) y **efectos de Copper** (degradado de `COLOR00`,
/// fine-scroll parcheable, paleta) sobre un `copper::Scheduler`, sin que el juego nombre registros
/// ni compute direcciones. Encima del `Scheduler`/`Plan` existentes.
///
/// ```cpp
/// eng::copper::SchedulerT<false> sched { copper_block };
/// eng::scene::emit_display(sched, { .planes_view = planes, .planes = 5u });
/// auto scroll = eng::scene::emit_fine_scroll(sched, 0u);
/// eng::scene::emit_gradient(sched, 0x41u, 0x4fu, 0x0111u);
/// ```

#include <eng/core/types/domains.hpp>
#include <eng/graphics/copper/copper.hpp>
#include <eng/graphics/copper/scheduler.hpp>
#include <eng/graphics/frame_plan.hpp>

namespace eng::scene {

/// Descripción de un display de bajo-res estándar (valores por defecto: 320x256, 5 planos).
struct DisplayDesc {
	eng::u16 diwstrt = 0x2c81u;
	eng::u16 diwstop = 0x2cc1u;
	eng::u16 ddfstrt = 0x0038u;
	eng::u16 ddfstop = 0x00d0u;
	eng::u16 bplcon0 = 0x5200u; ///< 5 planos + COLOR
	eng::u16 bplcon2 = 0u;
	eng::u8 planes = 5u;
	eng::u16 bytes_per_row = 40u; ///< bytes por fila de un plano (320 px / 8)
	eng::ChipPlaneView planes_view {}; ///< base Chip de los planos (DMA)
	eng::u16 dmacon = static_cast<eng::u16>(::eng::copper::DmaSetClear | ::eng::copper::DmaMaster |
						::eng::copper::DmaCopper | ::eng::copper::DmaBitplane |
						::eng::copper::DmaBlitter);
};

/// Emite el display estándar en `sched`: DMACON/BPLCONx, módulos interleaved, DIW/DDF y `BPLxPT`
/// (un plano por paso `row_bytes`). El juego no ve registros.
template <class Scheduler>
void emit_display(Scheduler& sched, const DisplayDesc& d) {
	sched.move(::eng::copper::Register::DMACON, d.dmacon);
	sched.move(::eng::copper::Register::BPLCON0, d.bplcon0);
	sched.move(::eng::copper::Register::BPLCON1, 0u);
	sched.move(::eng::copper::Register::BPLCON2, d.bplcon2);
	// Interleaved: tras leer una fila de un plano (bytes_per_row), el siguiente plano está a
	// bytes_per_row; el "salto" a la fila siguiente es (planes-1)*bytes_per_row.
	const eng::u16 mod = static_cast<eng::u16>(d.bytes_per_row * (d.planes - 1u));
	sched.move(::eng::copper::Register::BPL1MOD, mod);
	sched.move(::eng::copper::Register::BPL2MOD, mod);
	sched.move(::eng::copper::Register::DIWSTRT, d.diwstrt);
	sched.move(::eng::copper::Register::DIWSTOP, d.diwstop);
	sched.move(::eng::copper::Register::DDFSTRT, d.ddfstrt);
	sched.move(::eng::copper::Register::DDFSTOP, d.ddfstop);
	for (eng::u8 p = 0u; p < d.planes; ++p) {
		sched.move_bitplane_pointer(p, d.planes_view.address(
						      static_cast<eng::s32>(static_cast<eng::u32>(p) *
									    d.bytes_per_row)));
	}
}

/// Emite una **paleta** `count` colores (registros `COLOR00..`).
template <class Scheduler>
void emit_palette(Scheduler& sched, const eng::u16* palette, eng::u8 count) {
	for (eng::u8 i = 0u; i < count; ++i) {
		sched.move(::eng::copper::color_register(i), palette[i]);
	}
}

/// **Degradado** de `COLOR00` en las líneas `[first, last]`: `step * (linea - first + 1)`.
template <class Scheduler>
void emit_gradient(Scheduler& sched, eng::u8 first, eng::u8 last, eng::u16 step) {
	eng::u16 k = 1u;
	for (eng::u8 line = first; line <= last; ++line, ++k) {
		sched.wait_line(line);
		sched.move(::eng::copper::Register::COLOR00, static_cast<eng::u16>(step * k));
	}
}

/// **Fine-scroll horizontal** parcheable por frame: MOVE de `BPLCON1` con `handle` tipado.
template <class Scheduler>
[[nodiscard]] ::eng::copper::PatchHandle emit_fine_scroll(Scheduler& sched,
							  eng::u16 initial = 0u) {
	return sched.patchable(::eng::copper::Register::BPLCON1, initial);
}

} // namespace eng::scene
