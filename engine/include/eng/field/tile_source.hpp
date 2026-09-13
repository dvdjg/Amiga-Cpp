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

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>

namespace eng::field {

/// Contrato del accesor de tiles: `tile_at(celda)->u16` e `is_empty(tile)->bool`.
template <class T>
concept TileSource = requires(const T& t, eng::s32 x, eng::s32 y, eng::u16 g) {
	t.tile_at(x, y);
	t.is_empty(g);
};

/// División entera hacia abajo (correcta con coordenadas negativas), sin `%`.
constexpr eng::s32 floor_div(eng::s32 a, eng::s32 b) {
	return (a >= 0) ? (a / b) : -(((-a) + b - 1) / b);
}

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
