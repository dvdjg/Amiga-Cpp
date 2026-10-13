#pragma once

/// \file fast_bob_margins.hpp
/// **Cálculo de los márgenes del FastCopy** (F5 de `ROADMAP_SPRITES_COMPLETO.md` §3.4): el
/// asset declara el **desplazamiento máximo por eje** del BOB (píxeles) y este cálculo
/// devuelve el padding transparente que debe llevar la hoja para que la copia (`$F0`,
/// `BobDraw::Opaque` + `FastBobLayer`) tape por sí sola la posición anterior, sin `clear`
/// ni máscara. Con doble buffer los márgenes se **doblan** (entre el frame que pinta y el
/// siguiente hay hasta dos pasos de movimiento). Ver `dual-playfield-fastbobs.md` y
/// `scene/bobs.hpp` (`FastBobLayer`).
///
/// El pipeline de cocina del asset usa el resultado para generar la hoja con padding y el
/// **ancla**: el contenido queda en `(left, top)` dentro de la hoja, de modo que el juego
/// sigue posicionando el BOB por su esquina lógica (el `FastBobLayer` dibuja la hoja con
/// márgenes desde `x - left`, `y - top`).
///
/// Lógica pura (sin hardware, sin heap): host-testable.

#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Márgenes (padding transparente) alrededor del contenido y tamaño final de la hoja.
struct FastBobMargins {
	u16 left = 0u;     ///< margen por la izquierda (px)
	u16 right = 0u;    ///< margen por la derecha
	u16 top = 0u;      ///< margen superior
	u16 bottom = 0u;   ///< margen inferior
	u16 padded_w = 0u; ///< ancho final de la hoja (`w + left + right`)
	u16 padded_h = 0u; ///< alto final de la hoja (`h + top + bottom`)
};

/// Calcula los márgenes del FastCopy de un contenido `w`×`h` con desplazamiento máximo por
/// eje (`max_left`/`max_right`/`max_up`/`max_down`, píxeles por frame). Con `double_buffer`
/// los cuatro se **doblan**. Los valores son de un asset de sprite (decenas de píxeles):
/// la suma se mantiene en `u16` sin recortes (el contrato del llamador).
[[nodiscard]] constexpr FastBobMargins fast_bob_margins(u16 w, u16 h, u16 max_left,
							u16 max_right, u16 max_up, u16 max_down,
							bool double_buffer = false) noexcept {
	FastBobMargins m {};
	const u16 mul = double_buffer ? 2u : 1u;
	m.left = static_cast<u16>(max_left * mul);
	m.right = static_cast<u16>(max_right * mul);
	m.top = static_cast<u16>(max_up * mul);
	m.bottom = static_cast<u16>(max_down * mul);
	m.padded_w = static_cast<u16>(w + m.left + m.right);
	m.padded_h = static_cast<u16>(h + m.top + m.bottom);
	return m;
}

} // namespace eng::graphics
