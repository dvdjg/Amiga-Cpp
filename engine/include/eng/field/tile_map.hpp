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
///
/// `TileLayerMapT<Width>` permite fijar el número de tiles en X en tiempo de
/// compilación. Es opcional: `TileLayerMap` equivale a `TileLayerMapT<0>`. Para
/// mapas generados se recomienda un ancho potencia de dos (64, 128, 256...):
/// el compilador puede plegar el índice y, si el wrap coincide con el ancho, no
/// se paga un módulo runtime en el camino caliente.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/field/tile_access.hpp>
#include <eng/field/tile_source.hpp>

namespace eng::playfield {

/// Mapa denso de tiles con ancho opcionalmente fijo.
///
/// \tparam FixedWidth ancho en tiles conocido en compilación; `0` conserva el
///         ancho runtime clásico. Cuando es distinto de cero, `tile_at` indexa
///         con esa constante (el campo `width` debe valer lo mismo).
template <eng::u16 FixedWidth = 0u>
struct TileLayerMapT {
	/// Ancho fijo de la variante; `0` = se usa `width` en runtime.
	static constexpr eng::u16 fixed_width = FixedWidth;

	eng::Span<const eng::u16> cells {};
	/// En la variante fija nace con `FixedWidth`; si se cambia, debe seguir
	/// coincidiendo con el parámetro de plantilla para no desincronizar el índice.
	eng::u16 width = FixedWidth, height = 0, wrap_x = 0, wrap_y = 0, edge_tile = 0;
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
		// Camino caliente: la mayoría de lecturas caen dentro del mapa. Evita
		// `%` de 32 bits (libcall) en `wrap_coordinate` para cada celda.
		const eng::s32 w = FixedWidth != 0u ? FixedWidth : width;
		const eng::s32 h = height;
		const bool plain_x = wrap_x == 0 || wrap_x == w;
		const bool plain_y = wrap_y == 0 || wrap_y == h;
		if (plain_x && plain_y && tx >= 0 && tx < w && ty >= 0 && ty < h) {
			return cells.at(static_cast<eng::u32>(tx + ty * w));
		}
		eng::s32 x = tx, y = ty;
		if (!resolve_tile_coord(x, y, w, h, wrap_x, wrap_y)) return edge_tile;
		return cells.at(static_cast<eng::u32>(x + y * w));
	}
};

/// Mapa denso clásico con ancho runtime.
using TileLayerMap = TileLayerMapT<0u>;

/// Atajo para mapas generados con un ancho fijo. Se recomienda que `Width` sea
/// potencia de dos para que el índice sea un desplazamiento.
template <eng::u16 Width>
using FixedTileLayerMap = TileLayerMapT<Width>;

} // namespace eng::playfield
