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
#include <eng/field/tile_source.hpp>

namespace eng::field {

struct TileLayerMap {
	eng::Span<const eng::u16> cells {};
	eng::u16 width = 0, height = 0, wrap_x = 0, wrap_y = 0, edge_tile = 0;
	/// Tile valor `empty_tile` (0xFFFF = desactivado) = "no se pinta".
	eng::u16 empty_tile = 0xFFFF;
	/// Modo DISPERSO opcional: si `use_sparse`, los tiles se leen por chunks
	/// (compatible con los "mapas infinitos" de Tiled). `width/height/wrap_*` siguen
	/// describiendo los límites del mundo para el scroll.
	SparseTileMap<16> sparse {};
	bool use_sparse = false;

	/// Activa el modo disperso con el directorio de chunks dado (sincroniza `empty`).
	void set_sparse(eng::Span<const typename SparseTileMap<16>::Chunk> chunks) {
		sparse.chunks = chunks;
		sparse.empty_tile = empty_tile;
		use_sparse = true;
	}
	static eng::s32 wrap_coordinate(eng::s32 value, eng::u16 period) {
		// Comparte `wrap_period` (core): máscara con period potencia de dos
		// (evita `__modsi3` en el 68000) y camino general con negativos.
		return wrap_period(value, period);
	}
	/// Cumple `TileSource`: un tile es "vacío" si es `empty_tile`.
	constexpr bool is_empty(eng::u16 g) const { return g == empty_tile; }
	/// Cumple `TileMap`: hay datos si el modo activo tiene celdas o chunks.
	constexpr bool has_data() const {
		return use_sparse ? !sparse.chunks.empty() : !cells.empty();
	}
	eng::u16 tile_at(eng::s32 tx, eng::s32 ty) const {
		if (use_sparse) return sparse.tile_at(tx, ty);
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
