#pragma once

/// \file tile_bake.hpp
/// **Cocido de un mapa por capas a un tileset indexado** (`eng::gfx::proc`): el puente
/// entre la composición procedural (`LayeredTileMap` + `render_layered_map` /
/// `blit_tile`) y el **motor de tiles** (banco interleaved + `TileLayerMap`): cada celda
/// se compone una vez en su cuadrado de `N×N` píxeles y las celdas **idénticas se
/// deduplican** a una sola pieza del banco. El mapa final es una rejilla de índices de
/// pieza (lo que consume el driver: X-Limited, `Blitter`, …).
///
/// ```text
///   LayeredTileMap (ground/decal/overhead)        BakedIndexedMap
///     celda (x,y) ──blit_tile(N×N)──► celda final    tiles[k][N*N]   (1 B/píxel, únicas)
///                     │  dedup (memcmp)                cells[W*H]    ÍNDICE de pieza
///                     ▼                                  count/overflow
/// ```
///
/// Todo es **generación** (`constexpr`): el pipeline lo cocina en compilación (los
/// tiles quedan en rodata) y el motor de tiles pinta el mapa con el Blitter — **sin
/// compose por píxel ni C2P por frame**.
///
/// ## Estado de verificación: VERIFICADA (demo 121)
/// La demo `121_proc_corner_layers` cocina el mundo (esquinas + caminos + castillo +
/// casas + árboles) con esta función y lo pinta por `XlimitedScene` con el Blitter;
/// el dedup y la equivalencia píxel a píxel se verifican en `HOST-448`.

#include <eng/core/types/types.hpp>
#include <eng/graphics/proc/bitmap_buffer.hpp>
#include <eng/graphics/proc/layered_map.hpp>

namespace eng::gfx::proc {

/// **Mapa cocido**: las piezas únicas (1 B/píxel, `N*N` c/u) y la rejilla de índices.
/// `cells == kEmptyTile` en una celda sin suelo (no se pinta).
/// \tparam W,H celdas del mapa. \tparam N lado del tile. \tparam MaxTiles capacidad fija.
template <eng::u16 W, eng::u16 H, eng::u16 N, eng::u16 MaxTiles>
struct BakedIndexedMap {
	/// Piezas únicas: `tiles[k][y*N+x]` (índices de color; la usan como banco).
	eng::u8 tiles[MaxTiles][N * N] {};
	/// Índice de pieza de cada celda (`kEmptyTile` = sin suelo).
	eng::u16 cells[W * H] {};
	/// Piezas cocidas (`<= MaxTiles`).
	eng::u16 count = 0u;
	/// Celdas truncadas por falta de capacidad (fallo de forma visible).
	eng::u16 overflow = 0u;

	/// Vista contigua del banco de piezas (`count·N·N` bytes) — lo que consume el
	/// driver de tiles (`TileBankBytes` de X-Limited, stride `N*N`).
	[[nodiscard]] constexpr eng::Span<const eng::u8> bank_view() const {
		return {&tiles[0][0], eng::usize(count) * N * N};
	}
	/// Rejilla de índices como mapa del field (`map.cells = baked.cells`).
	[[nodiscard]] constexpr eng::Span<const eng::u16> cells_view() const {
		return {cells, eng::usize(W) * H};
	}
};

/// **Cocina** el mapa por capas: por celda compone `ground → decal → overhead` (los
/// proveedores traducen índice → bitmap, como en `render_layered_map`) y la deduplica
/// contra las piezas ya cocidas (comparación byte a byte; típico: 16 transiciones de
/// esquinas + las piezas de estructura que el mundo use).
/// \tparam W,H,N geometría. \tparam MaxTiles capacidad. \tparam GroundFn,DecalFn,OverFn
///         callables `const BitmapBuffer<N,N>&(eng::u16 index)`.
/// \param map mapa por capas (capas ya rellenas). \param ground_of,decal_of,over_of
///        proveedores por capa. \return las piezas únicas y la rejilla de índices.
template <eng::u16 W, eng::u16 H, eng::u16 N, eng::u16 MaxTiles, class GroundFn, class DecalFn,
	  class OverFn>
[[nodiscard]] constexpr BakedIndexedMap<W, H, N, MaxTiles> bake_layered_map(
	const LayeredTileMap<W, H, N>& map, GroundFn&& ground_of, DecalFn&& decal_of,
	OverFn&& over_of) {
	BakedIndexedMap<W, H, N, MaxTiles> out {};
	BitmapBuffer<N, N> cell {};
	for (eng::u16 cy = 0u; cy < H; ++cy) {
		for (eng::u16 cx = 0u; cx < W; ++cx) {
			const eng::u32 i = eng::u32(cy) * W + cx;
			const eng::u16 g = map.cell(MapLayer::Ground, cx, cy);
			if (g == kEmptyTile) {
				out.cells[i] = kEmptyTile;
				continue;
			}
			// Composición de la celda (una vez): suelo opaco y capas transparentes.
			blit_tile(cell, ground_of(g), 0u, 0u, false);
			const eng::u16 d = map.cell(MapLayer::Decal, cx, cy);
			if (d != kEmptyTile) blit_tile(cell, decal_of(d), 0u, 0u, true);
			const eng::u16 o = map.cell(MapLayer::Overhead, cx, cy);
			if (o != kEmptyTile) blit_tile(cell, over_of(o), 0u, 0u, true);
			// Dedup: ¿ya está esta celda en el banco?
			eng::u16 found = out.count;
			for (eng::u16 k = 0u; k < out.count; ++k) {
				bool same = true;
				for (eng::u16 p = 0u; p < N * N && same; ++p) {
					same = out.tiles[k][p] == cell.pixels[p];
				}
				if (same) {
					found = k;
					break;
				}
			}
			if (found == out.count) {
				if (out.count >= MaxTiles) {
					++out.overflow;
					out.cells[i] = 0u; // celda truncada (visible vía `overflow`)
					continue;
				}
				for (eng::u16 p = 0u; p < N * N; ++p) out.tiles[out.count][p] = cell.pixels[p];
				found = out.count;
				++out.count;
			}
			out.cells[i] = found;
		}
	}
	return out;
}

} // namespace eng::gfx::proc
