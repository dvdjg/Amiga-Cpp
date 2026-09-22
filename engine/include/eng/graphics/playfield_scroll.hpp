#pragma once

/// \file playfield_scroll.hpp
/// **Convenciones de scroll de playfield** compartidas por los drivers de display y el
/// efecto `effects::FineScroll`: el retardo fino de `BPLCON1`, el `DDFSTRT` adelantado y el
/// coarse del puntero de bitplanes. Se aíslan aquí para que el cálculo del scroll (que hoy
/// se repite en varios sitios) tenga una única fuente.
///
/// Convención de hardware (AHRM 3rd ed.): `BPLCON1` es un *delay*; `DDFSTRT=$30` adelanta el
/// fetch **1 word** a la izquierda de la ventana, de modo que `display_start == scroll_x` es
/// continuo en todo el rango (fine 15 -> 0 sin salto). El puntero de bitplanes apunta al
/// coarse `(scroll_x - 1) & ~15` y el fine se programa en `BPLCON1`.

#include <eng/core/types.hpp>

namespace eng::graphics {

/// `DDFSTRT` con **1 word de margen** a la izquierda (fetch de una columna extra), requisito
/// del scroll fino continuo. Equivale a `$38 - $8`.
inline constexpr u16 fine_scroll_ddfstrt = 0x0030u;

/// **Retardo de scroll fino** (0..15): píxeles que hay que retardar para mostrar el
/// contenido desplazado `x` px a la izquierda. Es el valor de los nibbles de `BPLCON1`
/// (bajo = PF1, alto = PF2). Para `x & 15 == 0` el retardo es 0.
[[nodiscard]] constexpr u16 fine_delay(u16 x) noexcept {
	return static_cast<u16>((16u - (x & 15u)) & 15u);
}

/// **Coarse** (múltiplo de 16 px) del puntero de bitplanes para `scroll_x`, coherente con el
/// `DDFSTRT` adelantado: `(scroll_x - 1) & ~15`. El mínimo representable es 1 (con 0 el
/// puntero apuntaría 16 px antes del arranque del buffer).
[[nodiscard]] constexpr u16 fine_scroll_coarse(u16 scroll_x) noexcept {
	return static_cast<u16>((scroll_x - 1u) & 0xfff0u);
}

} // namespace eng::graphics
