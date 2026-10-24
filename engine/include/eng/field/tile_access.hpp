#pragma once

/// \file tile_access.hpp
/// Política común de coordenadas para los accesores de tiles: wrap toroidal y
/// borde acotado. No impone el almacenamiento (denso, disperso, streaming o
/// asset): solo resuelve `(x, y)` en las coordenadas válidas del mapa.
///
/// El camino caliente del mapa denso conserva su fast path directo; este helper
/// se usa en los adaptadores y en el camino general, donde sí hay periodos
/// runtime y no conviene duplicar la semántica de wrap/edge.

#include <eng/core/math/fast_div.hpp>
#include <eng/core/types/types.hpp>

namespace eng::playfield {

/// Resuelve una coordenada de celda contra el mapa.
///
/// Si `wrap_x`/`wrap_y` son distintos de cero, la coordenada se envuelve en
/// `[0, periodo)`. Si el eje es acotado (`wrap_* == 0`) y `width`/`height` no
/// son cero, una coordenada fuera de rango rechaza la consulta. Un límite de
/// cero significa "sin límite" para ese eje, como en `TileMapView`.
///
/// \param x       coordenada X de entrada/salida (se normaliza si envuelve).
/// \param y       coordenada Y de entrada/salida (se normaliza si envuelve).
/// \param width   ancho del mapa en celdas (0 = sin límite).
/// \param height  alto del mapa en celdas (0 = sin límite).
/// \param wrap_x  periodo X (0 = eje acotado).
/// \param wrap_y  periodo Y (0 = eje acotado).
/// \return `true` si la coordenada es válida o quedó envuelta; `false` si cae
///         fuera de un eje acotado.
[[nodiscard]] constexpr bool resolve_tile_coord(eng::s32& x, eng::s32& y,
                                                eng::s32 width, eng::s32 height,
                                                eng::u16 wrap_x, eng::u16 wrap_y) noexcept {
	if (wrap_x != 0u) {
		x = eng::wrap_period(x, wrap_x);
	} else if (width != 0 && (x < 0 || x >= width)) {
		return false;
	}
	if (wrap_y != 0u) {
		y = eng::wrap_period(y, wrap_y);
	} else if (height != 0 && (y < 0 || y >= height)) {
		return false;
	}
	return true;
}

} // namespace eng::playfield
