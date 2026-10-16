#pragma once

/// \file actor_target.hpp
/// **Puente actor ↔ playfield corkscrew**: construye el `BobTarget` de un playfield
/// X-Limited (anillo) para el emisor de actores (`scene::actor_emit`) y fija el
/// contrato del anillo en esa capa, que hasta ahora vivía solo en la fontanería de
/// las demos:
///
///   - **y del objeto = LÍNEA DEL ANILLO** (fila visible + `display_offset`), la misma
///     coordenada que usan los blits de mundo. La vía natural con `ActorDesc` es dejar
///     `desc.y` en píxeles VISIBLES y sumar el offset de la vista en `desc.offset.y`
///     (o pasar directamente la línea del anillo).
///   - El rect puede **cruzar la costura**: con `ActorEmitContext::ring_height =
///     hv.display_height` el emisor la reparte en dos trabajos lineales
///     (`graphics/bob_ring.hpp`: dibujo, borrado, save-under y restore; todas las
///     variantes de BOB). Sin `ring_height` el emisor exige el rect entero dentro del
///     bucle — `actor_fits_ring` es el chequeo barato para ese contrato (y para decidir
///     en la lógica de juego si el actor «está en pantalla» sin envolver).
///   - La superficie destino es el anillo completo con layout **interleaved**
///     (`[línea][plano]`), `row_bytes` = `bitmap_bytes_per_row`.
///   - **Clip**: `ActorEmitContext::clip` se compara contra el rect tal cual, así que si el
///     offset incluye `display_offset` el clip debe cubrir las LÍNEAS DEL ANILLO
///     (`{0, 0, viewport_w, display_height}`), no solo la ventana visible. Con
///     `ring_height != 0` el clip solo recorta X y exige el rect dentro del bucle.
///   - **Pack del BOB**: en destinos interleaved `bob_draw` exige `BobMaskPack::InterleavedPair`
///     ([imagen][máscara] por fila y **plano del destino**); el pack suelto se rechaza
///     (dispatch documentado en `bob.hpp` y fijado por HOST-453).
///
/// El objetivo es que un juego dibuje entidades de mundo (`ActorStore` + `Animation`)
/// sobre un X-Limited sin reimplementar la fontanería de bandas/máscaras — el hueco que
/// descubrió la demo 121 (`OBJECT_SYSTEM.md` §14.8).
///
/// ```cpp
/// const auto hv = scene.bg().hardware_view();
/// eng::graphics::BobTarget ring = eng::playfield::make_actor_target(hv);
/// // ... en el update:
/// a.desc.offset.y = eng::s16(hv.display_offset);   // visible -> anillo (una vez)
/// eng::scene::ActorEmitContext ctx {};
/// ctx.targets = eng::Span<const eng::graphics::BobTarget> {&ring, 1u};
/// ctx.clip = DirtyRect {0, 0, s16(hv.viewport_w), s16(hv.display_height)};
/// ctx.ring_height = hv.display_height;  // reparto en la costura (0 = lineal)
/// eng::scene::actor_emit(plan, a, ctx);
/// ```

#include <eng/core/types/types.hpp>
#include <eng/field/playfield_base.hpp>
#include <eng/graphics/bob.hpp>

namespace eng::playfield {

/// **Línea del anillo** de una fila visible: `visible_y + display_offset` (sin envolver;
/// el llamador decide con `actor_fits_ring` si el rect cabe antes de emitir).
/// \param view vista de hardware del playfield.
/// \param visible_y fila visible (0..viewport_h-1).
/// \return línea del anillo (puede exceder `display_height` si el rect no cabe).
[[nodiscard]] constexpr eng::s16 actor_ring_y(const PlayfieldHardwareView& view,
					      eng::s16 visible_y) {
	return static_cast<eng::s16>(visible_y + static_cast<eng::s16>(view.display_offset));
}

/// ¿El rect de `h` filas en `ring_y` cabe entero en el bucle del anillo (sin cruzar la
/// costura)? El emisor YA reparte los rects que la cruzan (`ActorEmitContext::ring_height`);
/// esta comprobación es para quien necesite saberlo de antemano (validación de setup,
/// lógica de «visible completa», decisiones de la demo) sin emitir.
/// \param view vista de hardware. \param ring_y línea del anillo. \param h alto en filas.
/// \return `true` si `0 ≤ ring_y` y `ring_y + h ≤ display_height`.
[[nodiscard]] constexpr bool actor_fits_ring(const PlayfieldHardwareView& view, eng::s16 ring_y,
					     eng::u16 h) {
	if (ring_y < 0 || view.display_height == 0u) {
		return false;
	}
	return eng::u32(ring_y) + eng::u32(h) <= eng::u32(view.display_height);
}

/// **`BobTarget` del anillo** para `scene::actor_emit`: cubre la asignación completa
/// (`plane_bytes`), con la geometría que usa el Blitter en el corkscrew
/// (`row_bytes` = bytes por fila y plano, `[línea][plano]` interleaved).
/// \param view vista de hardware del playfield.
/// \return destino listo para el emisor de actores (o para `bob_draw`).
[[nodiscard]] inline eng::graphics::BobTarget make_actor_target(const PlayfieldHardwareView& view) {
	return eng::graphics::make_bob_target(
		eng::ChipView<eng::PlaneTag> {view.bitplanes, view.plane_bytes}, view.bitmap_bytes_per_row,
		view.display_height, view.planes, eng::graphics::BobLayout::Interleaved);
}

} // namespace eng::playfield
