#pragma once

/// \file bob_ring.hpp
/// **BOBs sobre bitmaps cíclicos** (costura/«split» del corkscrew X-Limited y afines):
/// si el rect del objeto cruza el FINAL del bucle del anillo, la parte que sobra debe
/// dibujarse (o borrarse/guardarse) al PRINCIPIO del mismo bloque — otro tramo de
/// memoria dentro del anillo. El Blitter no sabe envolver: cada operación se emite como
/// **hasta dos trabajos lineales**, el segundo con la línea destino 0 y la fuente
/// avanzada las filas ya consumidas.
///
/// Geometría (anillo de `N` líneas; la ventana visible lo recorre con el split del
/// Copper — `PlayfieldHardwareView::split_line`):
///
///   ┌──────────────────────┐        rect del objeto en líneas del anillo
///   │ ...                  │        `ring_y` (y del ancla en el anillo, ≥ 0)
///   │ N-2  ▓▓▓▓▓▓  parte 1 │ ← job 1: altura `seg = N − ring_y`
///   │ N-1  ▓▓▓▓▓▓          │
///   │ 0    ▓▓▓▓▓▓  parte 2 │ ← job 2: altura `h − seg`, filas del arte
///   │ 1    ▓▓▓▓▓▓          │   `seg..h` (fuente avanzada `seg` filas)
///   │ ...                  │
///   └──────────────────────┘
///
/// Contrato (idéntico al camino lineal, extendido al bucle):
///   - **Cubre TODAS las variantes del BOB**: dibujo (`Or`/`Opaque`/`And`/`CookieCut`),
///     layouts (planar/interleaved), empaquetados de máscara (`SeparatePlane`/
///     `InterleavedPair`) y las cajas de borrado/save-under — todas delegan en los
///     emisores lineales (`draw_rows`, `bob_erase_box`, `bob_save_box`,
///     `bob_restore_box`), que son la **única fuente** de cada receta.
///   - `ring_y` viaja en **líneas del anillo** (la misma coordenada que los blits de
///     mundo; en `ActorDesc` es `desc.y + desc.offset.y`, ver `field/actor_target.hpp`).
///   - `ring_height == 0` = **sin anillo** → reenvía al camino lineal (un solo `if`, sin
///     copia de lógica): los consumidores lineales (playfields planos, espejo,
///     doble-buffer) no pagan nada.
///   - Sin cruce (`ring_y + h ≤ ring_height`) se emite UN único trabajo por la vía
///     normal: el coste del soporte es **una comparación `seg == h`** por operación y
///     frame (del orden de nanosegundos frente a los miles de ciclos del blit).
///   - `h > ring_height` (objeto más alto que el bucle) o `ring_y` fuera del anillo →
///     rechazo (`false` en el dibujo; `false` en save/restore porque no hay fondo que
///     guardar/restaurar); `ring_y < 0` → nada que hacer (`true`, mismo contrato que
///     `y < 0` en el camino lineal).
///   - **Save-under**: las filas del buffer de guardado ESPEJAN las filas de la caja
///     (fila `seg` de la caja ↔ fila `seg` del buffer), de modo que `bob_restore_box_ring`
///     (que recalcula la partición desde el rect guardado) restaura byte a byte.
///
/// Verificación: HOST-453 (actor en la costura con save-under y variantes Or flat/planar,
/// oráculo = ejecutor software del plan). Contraste de hardware: el reparto es el mismo
/// que usa el emisor de mundo (`xlimited_playfield.hpp::add_world_bitmap`, bucle
/// `while (remaining)`), y la ventana fina del barrel cita AHRM 6 / WinUAE
/// `blitter.cpp:530-531` (ver `bob.hpp`).

#include <eng/core/types/types.hpp>
#include <eng/graphics/bob.hpp>

namespace eng::graphics {

/// Partición de un rect de `h` filas que empieza en `ring_y` sobre un anillo de
/// `ring_height` líneas: `top` filas antes de la costura (líneas `ring_y..N-1`) y
/// `bottom` filas que envuelven (líneas `0..`). Sin cruce → `bottom == 0`.
/// `ring_height == 0` = sin anillo (`top = h`). NO valida: los wrappers comprueban
/// antes el encaje (`h ≤ ring_height`, `ring_y` dentro del anillo).
struct RingSegments {
	eng::u16 top = 0u;
	eng::u16 bottom = 0u;
};

/// Calcula la partición (ver `RingSegments`). Coste fijo (una comparación + una resta).
/// \param ring_y línea del anillo del borde superior del rect (≥ 0 y < ring_height).
/// \param h filas del rect. \param ring_height líneas del bucle (0 = sin anillo).
[[nodiscard]] constexpr RingSegments ring_segments(eng::s16 ring_y, eng::u16 h,
						   eng::u16 ring_height) {
	RingSegments s {};
	if (ring_height == 0u || eng::u32(ring_y) + eng::u32(h) <= eng::u32(ring_height)) {
		s.top = h;
		return s;
	}
	s.top = static_cast<eng::u16>(eng::u32(ring_height) - eng::u32(ring_y));
	s.bottom = static_cast<eng::u16>(h - s.top);
	return s;
}

/// **Dibuja** el `frame` del objeto en `(x, ring_y)` sobre un anillo de `ring_height`
/// líneas, partiendo el rect en la costura si la cruza (ver cabecera del fichero).
/// Delega en `bob_draw`/`draw_rows`: admite todas las variantes (dibujo, layouts y
/// empaquetados de máscara). Sin cruce, un único trabajo por la vía lineal.
/// \param plan  plan del frame. \param bob objeto. \param frame índice en la hoja.
/// \param x     x en píxeles (no envuelve: el anillo es vertical).
/// \param ring_y línea del anillo del borde superior. \param ring_height líneas del bucle.
/// \param t     geometría del anillo (`[línea][plano]`) — `make_actor_target`.
/// \return `false` si `bob`/`t` no son válidos, frame fuera de rango, `ring_y` fuera del
///         anillo o `h > ring_height`; `true` si no había nada que dibujar.
[[nodiscard]] inline bool bob_draw_ring(FramePlan& plan, const Bob& bob, eng::u8 frame, eng::s16 x,
					eng::s16 ring_y, eng::u16 ring_height, const BobTarget& t) {
	using namespace bob_detail;
	if (!valid(bob, t) || frame >= bob.frame_count) {
		return false;
	}
	if (ring_height == 0u) {
		return bob_draw(plan, bob, frame, x, ring_y, t); // sin anillo: vía lineal exacta
	}
	if (ring_y < 0) {
		return true; // fuera por arriba (mismo contrato que el camino lineal)
	}
	if (eng::u32(ring_y) >= eng::u32(ring_height) || bob.height > ring_height) {
		return false; // el rect no cabe en el bucle: geometría inválida (validar en setup)
	}
	const RingSegments seg = ring_segments(ring_y, bob.height, ring_height);
	if (!draw_rows(plan, bob, frame, x, ring_y, 0u, seg.top, t)) {
		return false;
	}
	if (seg.bottom == 0u) {
		return true; // sin cruce: un solo trabajo, coste del soporte = esta comparación
	}
	// Parte que envuelve: línea 0 del anillo; las filas `seg.top..` del arte.
	return draw_rows(plan, bob, frame, x, 0, seg.top, seg.bottom, t);
}

/// **Borra una caja** `w×h` en `(x, ring_y)` sobre el anillo, partida en la costura.
/// Dos llamadas a `bob_erase_box` en el caso con cruce (alturas `seg` y `h − seg`).
[[nodiscard]] inline bool bob_erase_box_ring(FramePlan& plan, const Bob& bob, eng::u16 w,
					     eng::u16 h, eng::s16 x, eng::s16 ring_y,
					     eng::u16 ring_height, const BobTarget& t) {
	if (ring_height == 0u) {
		return bob_erase_box(plan, bob, w, h, x, ring_y, t);
	}
	if (ring_y < 0) {
		return true; // fuera por arriba: nada que borrar
	}
	if (eng::u32(ring_y) >= eng::u32(ring_height) || h > ring_height) {
		return false;
	}
	const RingSegments seg = ring_segments(ring_y, h, ring_height);
	if (!bob_erase_box(plan, bob, w, seg.top, x, ring_y, t)) {
		return false;
	}
	if (seg.bottom == 0u) {
		return true;
	}
	return bob_erase_box(plan, bob, w, seg.bottom, x, 0, t);
}

/// **Borra** la caja del objeto (`BobErase::ClearRect`) partiendo en la costura si el
/// rect la cruza; con otro algoritmo de borrado no hace nada (contrato de `bob_erase`).
[[nodiscard]] inline bool bob_erase_ring(FramePlan& plan, const Bob& bob, eng::s16 x,
					 eng::s16 ring_y, eng::u16 ring_height, const BobTarget& t) {
	if (bob.erase != BobErase::ClearRect) {
		return true;
	}
	return bob_erase_box_ring(plan, bob, bob.width, bob.height, x, ring_y, ring_height, t);
}

/// **Save-under (guardar)** de la caja `w×h` en `(x, ring_y)`, partida en la costura. El
/// buffer `save` conserva las filas alineadas con la caja (la parte que envuelve se
/// guarda en las filas `seg..h` del MISMO buffer, vía `subview`), para que el restore
/// pueda espejar la partición sin estado extra. Misma validación que `bob_save_box`.
[[nodiscard]] inline bool bob_save_box_ring(FramePlan& plan, const Bob& bob, eng::u16 w,
					    eng::u16 h, eng::s16 x, eng::s16 ring_y,
					    eng::u16 ring_height, const BobTarget& t,
					    eng::ChipView<eng::BobTag> save,
					    eng::u16 save_words_per_row, eng::u16 save_height) {
	if (ring_height == 0u) {
		return bob_save_box(plan, bob, w, h, x, ring_y, t, save, save_words_per_row,
				    save_height);
	}
	if (ring_y < 0 || eng::u32(ring_y) >= eng::u32(ring_height) || h > ring_height) {
		return false; // no hay fondo que guardar (mismo criterio que `y < 0` lineal)
	}
	const RingSegments seg = ring_segments(ring_y, h, ring_height);
	if (!bob_save_box(plan, bob, w, seg.top, x, ring_y, t, save, save_words_per_row,
			  save_height)) {
		return false;
	}
	if (seg.bottom == 0u) {
		return true;
	}
	// Filas `seg.top..` de la caja → filas `seg.top..` del buffer (paso del buffer =
	// `save_words_per_row` palabras); `save_height` sigue siendo la capacidad TOTAL.
	const eng::u32 off = static_cast<eng::u32>(seg.top) * save_words_per_row * 2u;
	return bob_save_box(plan, bob, w, seg.bottom, x, 0, t,
			    save.subview(off, save.size() - off), save_words_per_row, save_height);
}

/// **Save-under (restaurar)** de la caja `w×h` en `(x, ring_y)`, partida en la costura.
/// Inverso exacto de `bob_save_box_ring` (misma partición, mismo `subview` del buffer).
[[nodiscard]] inline bool bob_restore_box_ring(FramePlan& plan, const Bob& bob, eng::u16 w,
					       eng::u16 h, eng::s16 x, eng::s16 ring_y,
					       eng::u16 ring_height, const BobTarget& t,
					       eng::ChipView<eng::BobTag> save,
					       eng::u16 save_words_per_row, eng::u16 save_height) {
	if (ring_height == 0u) {
		return bob_restore_box(plan, bob, w, h, x, ring_y, t, save, save_words_per_row,
				       save_height);
	}
	if (ring_y < 0 || eng::u32(ring_y) >= eng::u32(ring_height) || h > ring_height) {
		return false;
	}
	const RingSegments seg = ring_segments(ring_y, h, ring_height);
	if (!bob_restore_box(plan, bob, w, seg.top, x, ring_y, t, save, save_words_per_row,
			     save_height)) {
		return false;
	}
	if (seg.bottom == 0u) {
		return true;
	}
	const eng::u32 off = static_cast<eng::u32>(seg.top) * save_words_per_row * 2u;
	return bob_restore_box(plan, bob, w, seg.bottom, x, 0, t,
			       save.subview(off, save.size() - off), save_words_per_row,
			       save_height);
}

} // namespace eng::graphics
