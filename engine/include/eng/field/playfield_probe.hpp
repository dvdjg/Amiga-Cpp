#pragma once

/// \file playfield_probe.hpp
/// **Lectura de píxel para verificación de render** (auto-chequeo de demos/juegos): da el
/// índice de color del píxel VISIBLE de un playfield corkscrew (X-Limited y afines) a
/// partir de su `PlayfieldHardwareView`, sin que el llamador tenga que conocer la
/// geometría del anillo.
///
/// El píxel visible `(sx, sy)` vive en la línea del anillo `(display_offset + sy) %
/// display_height` (la ventana del display empieza en `display_offset`; el resto del
/// anillo es la guarda). El layout es el **intercalado del corkscrew**: por línea, un
/// plano tras otro (`fila·planos + plano`), `bitmap_bytes_per_row` por plano, MSB =
/// píxel izquierdo (convención Amiga).
///
/// Contrato de sincronía: leer **después** de que el plan de blits haya terminado
/// (`execute_frame_plan` espera al último blit; ver su documentación en el backend).
/// `read_confirm` añade re-lectura de confirmación para tolerar lecturas aisladas
/// obsoletas (p. ej. un plano que el CPU no ve aún tras actividad DMA).
///
/// Uso típico (patrón de auto-chequeo, `COHERENCIA_DE_SETUP.md` §22):
///   const auto hv = scene.bg().hardware_view();
///   const eng::u8 c = eng::playfield::read_confirm(
///       [&] { return eng::playfield::read_visible_pixel(hv, x, y); }, /*expected=*/31u);

#include <eng/core/types/types.hpp>
#include <eng/field/playfield_base.hpp>

namespace eng::playfield {

/// Índice de color (0..2^planos-1) del píxel de la **línea del anillo** `ring` y columna
/// `sx` (coordenada de bitmap: el mismo x que los blits). A diferencia de
/// `read_visible_pixel`, no aplica la ventana ni el `display_offset`: sirve para
/// verificar las **guarda** del corkscrew (la parte envuelta de un objeto que cruza la
/// costura aterriza en las líneas `0..`, fuera de la ventana visible — `bob_ring.hpp`).
/// \param view vista de hardware del playfield. \param ring línea del anillo
///        (0..display_height-1). \param sx columna del bitmap (0..viewport_w-1).
/// \return índice de color del píxel (0 fuera de rango).
[[nodiscard]] inline eng::u8 read_ring_pixel(const PlayfieldHardwareView& view, eng::u16 ring,
					     eng::u16 sx) {
	if (view.bitplanes.cptr() == nullptr || view.planes == 0u || view.display_height == 0u) {
		return 0u;
	}
	if (sx >= view.viewport_w || ring >= view.display_height) {
		return 0u;
	}
	const eng::u8* const base = view.bitplanes.cptr();
	const eng::u32 bpr = view.bitmap_bytes_per_row;
	const eng::u32 byte_col = eng::u32(sx >> 3u);
	eng::u8 v = 0u;
	for (eng::u8 p = 0u; p < view.planes; ++p) {
		const eng::u8 byte =
			base[(eng::u32(ring) * eng::u32(view.planes) + eng::u32(p)) * bpr + byte_col];
		if ((byte & eng::u8(0x80u >> (sx & 7u))) != 0u) {
			v = eng::u8(v | eng::u8(1u << p));
		}
	}
	return v;
}

/// Índice de color (0..2^planos-1) del píxel visible `(sx, sy)` del playfield.
/// \param view vista de hardware del playfield (geometría y plano base en Chip).
/// \param sx,sy píxel VISIBLE (0..viewport_w-1, 0..viewport_h-1). Fuera de rango
///        devuelve 0 (el llamador de verificación decide qué es válido).
/// \return índice de color del píxel.
[[nodiscard]] inline eng::u8 read_visible_pixel(const PlayfieldHardwareView& view, eng::u16 sx,
						eng::u16 sy) {
	if (view.bitplanes.cptr() == nullptr || view.planes == 0u || view.display_height == 0u) {
		return 0u;
	}
	if (sx >= view.viewport_w || sy >= view.viewport_h) {
		return 0u;
	}
	// Línea del anillo de la fila visible: la ventana arranca en `display_offset` y el
	// anillo envuelve en `display_height` (guarda superior/inferior del corkscrew).
	const eng::u16 ring =
		eng::u16((eng::u32(view.display_offset) + eng::u32(sy)) % eng::u32(view.display_height));
	// Lectura única del píxel (misma implementación que `read_ring_pixel`).
	return read_ring_pixel(view, ring, sx);
}

/// **Re-lectura de confirmación**: llama a `read` hasta `passes` veces; devuelve en cuanto
/// el valor coincide con `expected` (evita pagar más lecturas en el caso bueno) o el
/// ÚLTIMO valor leído si ninguna pasada coincide. Sirve para separar fallos reales de
/// lecturas transitorias (p. ej. un plano que el CPU ve obsoleto en la primera pasada).
/// \param read     función `eng::u8()` de lectura.
/// \param expected valor correcto esperado.
/// \param passes   número máximo de lecturas (1 = sin confirmación).
/// \return el valor confirmado, o el último leído si no se confirmó.
template <class ReadFn>
[[nodiscard]] inline eng::u8 read_confirm(ReadFn&& read, eng::u8 expected, eng::u8 passes = 3u) {
	eng::u8 v = read();
	for (eng::u8 i = 1u; i < passes && v != expected; ++i) {
		v = read();
	}
	return v;
}

/// Atajo: `read_confirm` sobre `read_visible_pixel` de `(sx, sy)`.
[[nodiscard]] inline eng::u8 read_visible_pixel_confirm(const PlayfieldHardwareView& view, eng::u16 sx,
							eng::u16 sy, eng::u8 expected, eng::u8 passes = 3u) {
	return read_confirm([&view, sx, sy] { return read_visible_pixel(view, sx, sy); }, expected,
			    passes);
}

} // namespace eng::playfield
