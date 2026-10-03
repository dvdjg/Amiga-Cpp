#pragma once

/// \file world_render.hpp
/// **Puente de render del mundo retenido a un `Screen`**: materializa las capas `Fill` (rectángulos
/// opacos, orden por profundidad) y `Bitmap` (filas indexadas) trasladadas por la cámara de cada
/// capa y recortadas al viewport. Es lo que `App` ejecuta **antes** del `render` del juego; vive
/// aparte de la fachada para mantenerla compacta y para poder testearlo sin montar un `App`.
///
/// El `World` es una plantilla para no arrastrar `api/scene/world.hpp` a quien solo quiera el
/// contrato (`materialize_fill_layers`/`materialize_bitmap_layers`).

#include <eng/api/screen.hpp>
#include <eng/core/types/box.hpp>
#include <eng/core/types/domains.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>

namespace eng {

/// Emite las capas `Bitmap` del mundo como filas de color indexado sobre `screen` (run-length por
/// fila para reducir el número de `fill`). Devuelve `false` si alguna fila no cupo en el plan.
template <class World>
[[nodiscard]] bool materialize_bitmap_layers(World& world, u16 width, u16 height,
					     Screen screen) noexcept {
	auto emit_row = [&](u16 x, u16 y, ByteView<TextureTag> row) -> bool {
		usize i = 0u;
		while (i < row.size()) {
			const u8 color = row[i];
			usize end = i + 1u;
			while (end < row.size() && row[end] == color) ++end;
			if (!screen.fill(Box {static_cast<s16>(x + i), static_cast<s16>(y),
					      static_cast<u16>(end - i), 1u}, color)) {
				return false;
			}
			i = end;
		}
		return true;
	};
	// Una sola pasada: `materialize_bitmap_layers` es idempotente (cada fila se pinta a su color),
	// así que repetirla solo duplicaba el trabajo del frame.
	return world.materialize_bitmap_layers(emit_row, width, height);
}

/// Materializa el **mundo retenido** sobre `screen` en el orden de composición: primero las capas
/// `Fill` (fondo, cada una limpiada a color 0 y luego pintada de su color) y después las capas
/// `Bitmap`. Devuelve `false` si algo no cupo; el llamador lo refleja en su telemetría.
template <class World>
[[nodiscard]] bool materialize_world_layers(World& world, u16 width, u16 height,
					    Screen screen) noexcept {
	const bool fills_ok = world.materialize_fill_layers(
		[&](const Box& bounds, u8 color) {
			const bool cleared = screen.fill(bounds, 0u);
			const bool colored = screen.fill(bounds, color);
			return cleared && colored;
		},
		width, height);
	if (!fills_ok) return false;
	return materialize_bitmap_layers(world, width, height, screen);
}

} // namespace eng
