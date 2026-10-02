#pragma once

/// \file anim.hpp
/// **Animación de sprites de juego** (`eng::graphics::Anim`): una secuencia de frames de la hoja
/// con una **duración** (en frames de juego) por frame, opcionalmente en bucle. El juego la avanza
/// con `update()` y dibuja con `screen().sprite(sheet, anim.frame(), x, y)`: no conoce la hoja, el
/// Blitter ni los registros. La colisión de caja/píxel se resuelve con `Box::overlaps`/`Box::intersection`.

#include <eng/core/types/box.hpp>
#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::graphics {

/// Secuencia de frames con duración por frame. `frames`/`durations` son vistas **no propietarias**
/// (viven en la demo/game, a menudo `constexpr`): la animación no reserva ni copia.
struct Anim {
	eng::Span<const eng::u8> frames {};    ///< índices de frame de la hoja, en orden
	eng::Span<const eng::u8> durations {}; ///< duración (frames de juego) de cada `frame`
	bool loop = true;                      ///< al terminar, ¿vuelve al primero?

	u32 elapsed = 0u;  ///< frames de juego transcurridos en el frame actual
	u8 index = 0u;     ///< índice dentro de `frames`
	bool done = false; ///< animación sin bucle terminada

	[[nodiscard]] constexpr bool valid() const noexcept { return !frames.empty(); }
	/// Frame de la hoja que toca dibujar este frame de juego.
	[[nodiscard]] constexpr u8 frame() const noexcept {
		return frames.empty() ? 0u : frames[index < frames.size() ? index : 0u];
	}
	[[nodiscard]] constexpr bool finished() const noexcept { return done; }

	/// Avanza un frame de juego. En bucle al terminar (si `loop`).
	constexpr void update() noexcept {
		if (frames.empty() || done) {
			return;
		}
		const u32 dur = (index < durations.size()) ? durations[index] : 1u;
		if (++elapsed < dur) {
			return;
		}
		elapsed = 0u;
		if (++index < frames.size()) {
			return;
		}
		if (loop) {
			index = 0u;
		} else {
			index = static_cast<u8>(frames.size() - 1u);
			done = true;
		}
	}
	/// Reinicia desde el primer frame.
	constexpr void reset() noexcept {
		elapsed = 0u;
		index = 0u;
		done = false;
	}
	/// Área ocupada del sprite en `(x, y)` (para colisión con `Box::overlaps`).
	[[nodiscard]] static constexpr Box bounds_at(s16 x, s16 y, u16 w, u16 h) noexcept {
		return Box {x, y, w, h};
	}
};

} // namespace eng::graphics
