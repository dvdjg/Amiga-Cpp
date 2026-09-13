#pragma once

/// \file tile_source.hpp
/// Concepto `TileSource` (accesor de tiles) + implementación **dispersa por chunks**.
///
/// El motor de scroll consume el ACCESOR, no una matriz concreta: una capa puede ser
/// un mapa denso (`TileLayerMap`) o disperso (`SparseTileMap`) sin cambiar el
/// algoritmo. Los tiles iguales a `empty_tile` no se pintan (no gastan Blitter).
///
/// Compatible con los "mapas infinitos" de Tiled (`<chunk x y>`); ver
/// `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.

#include <eng/core/fast_div.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::field {

/// `eng::wrap_period` (core) disponible como `eng::field::wrap_period`.
using eng::wrap_period;

/// Contrato del accesor de tiles: `tile_at(celda)->u16` e `is_empty(tile)->bool`.
template <class T>
concept TileSource = requires(const T& t, eng::s32 x, eng::s32 y, eng::u16 g) {
	t.tile_at(x, y);
	t.is_empty(g);
};

/// Contrato que consume el scroll: un `TileSource` más los límites del mundo
/// (ancho/alto/wrap/edge) y el indicador `has_data`. Permite que el playfield no
/// dependa del almacén concreto (denso, disperso o streaming).
template <class T>
concept TileMap = TileSource<T> && requires(const T& m) {
	static_cast<eng::u16>(m.width);
	static_cast<eng::u16>(m.height);
	static_cast<eng::u16>(m.wrap_x);
	static_cast<eng::u16>(m.wrap_y);
	m.has_data();
};

/// División entera hacia abajo (correcta con coordenadas negativas), sin `%`.
constexpr eng::s32 floor_div(eng::s32 a, eng::s32 b) {
	return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b);
}

/// Vista de un mundo disperso/streaming con los límites que necesita el scroll.
/// El almacén (`Src`, p. ej. `SparseTileMap` o `StreamingWorldMap`) lo posee el
/// llamador y aquí se referencia: así el `prefetch` se hace sobre ESA instancia
/// (mientras el playfield solo consulta residentes). El wrap y el borde se
/// resuelven aquí, de modo que `Src` no necesita conocerlos.
template <class Src>
struct TileMapView {
	static_assert(TileSource<Src>, "Src debe cumplir TileSource");
	Src* src = nullptr;
	eng::u16 width = 0, height = 0, wrap_x = 0, wrap_y = 0, edge_tile = 0;

	constexpr bool has_data() const { return src != nullptr; }
	constexpr bool is_empty(eng::u16 g) const { return src != nullptr && src->is_empty(g); }
	eng::u16 tile_at(eng::s32 x, eng::s32 y) const {
		if (src == nullptr) return edge_tile;
		if (wrap_x) x = wrap_period(x, wrap_x);
		else if (width != 0 && (x < 0 || x >= static_cast<eng::s32>(width))) return edge_tile;
		if (wrap_y) y = wrap_period(y, wrap_y);
		else if (height != 0 && (y < 0 || y >= static_cast<eng::s32>(height))) return edge_tile;
		return src->tile_at(x, y);
	}
};

/// Mapa DISPERSO por chunks: sólo se almacenan las regiones pobladas. Cada chunk es
/// una rejilla `ChunkSize*ChunkSize` de tiles; `ChunkSize` es potencia de dos (la
/// coordenada local es una máscara, sin `%`). Los chunks ausentes devuelven
/// `empty_tile`. El directorio es lineal; para mapas muy grandes se indexa (§ roadmap).
template <eng::u16 ChunkSize = 16>
struct SparseTileMap {
	static_assert(ChunkSize != 0u && (ChunkSize & (ChunkSize - 1u)) == 0u,
	              "ChunkSize debe ser potencia de dos");
	struct Chunk {
		eng::s32 cx = 0, cy = 0;              // índice de chunk
		eng::Span<const eng::u16> cells {};   // ChunkSize*ChunkSize tiles
	};
	eng::Span<const Chunk> chunks {};
	eng::u16 empty_tile = 0xFFFFu;

	constexpr bool is_empty(eng::u16 g) const { return g == empty_tile; }

	eng::u16 tile_at(eng::s32 x, eng::s32 y) const {
		if (chunks.empty()) return empty_tile;
		const eng::s32 size = static_cast<eng::s32>(ChunkSize);
		const eng::s32 cx = floor_div(x, size);
		const eng::s32 cy = floor_div(y, size);
		const eng::u32 lx = static_cast<eng::u32>(x - cx * size);
		const eng::u32 ly = static_cast<eng::u32>(y - cy * size);
		for (eng::u32 i = 0; i < chunks.size(); ++i) {
			if (chunks[i].cx == cx && chunks[i].cy == cy) {
				return chunks[i].cells.at(ly * static_cast<eng::u32>(ChunkSize) + lx);
			}
		}
		return empty_tile;
	}
};

} // namespace eng::field
