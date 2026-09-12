#pragma once

/// \file tile_map.hpp
/// Mapa de tiles **lógico** (rejilla de índices), compartido por las capas de
/// scroll. No posee píxeles ni framebuffer: solo la rejilla (`u16` por celda) y
/// el wrapping toroidal/acotado. El tileset (píxeles) vive en la capa que lo
/// consume (`XLimitedPlayfield`), y el framebuffer de scroll está acotado
/// (~352/384 x alto), independiente del tamaño del mapa.
///
/// Se separó de `tile_field.hpp` al retirar el modelo circular
/// `TileFieldController` (sustituido por XYLimited, ver
/// `docs/engine/architecture/XYLIMITED_ALGORITMO_GENERICO.md`).

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::field {

struct TileLayerMap {
	eng::Span<const eng::u16> cells {};
	eng::u16 width = 0, height = 0, wrap_x = 0, wrap_y = 0, edge_tile = 0;
	/// Tile valor `empty_tile` (0xFFFF = desactivado) = "no se pinta".
	eng::u16 empty_tile = 0xFFFF;
	static eng::s32 wrap_coordinate(eng::s32 value, eng::u16 period) {
		// Los mapas de las demos son potencias de dos (256x128). En el 68000,
		// sustituir modulo por una máscara evita __modsi3 en cada tile. El camino
		// general conserva el contrato para mapas reales de cualquier tamaño.
		if ((period & static_cast<eng::u16>(period - 1u)) == 0u) {
			return value & static_cast<eng::s32>(period - 1u);
		}
		return ((value % static_cast<eng::s32>(period)) + period) % period;
	}
	eng::u16 tile_at(eng::s32 tx, eng::s32 ty) const {
		if (cells.empty() || width == 0 || height == 0) return edge_tile;
		eng::s32 x = tx, y = ty;
		if (wrap_x) x = wrap_coordinate(x, wrap_x);
		else if (x < 0 || x >= width) return edge_tile;
		if (wrap_y) y = wrap_coordinate(y, wrap_y);
		else if (y < 0 || y >= height) return edge_tile;
		return cells.at(static_cast<eng::u32>(x) + static_cast<eng::u32>(y) * width);
	}
};

} // namespace eng::field
