#pragma once

/// \file streaming_map.hpp
/// `WorldMap` disperso con **streaming**: mantiene residentes (en un pool de Chip RAM
/// del llamador) los chunks que cubren la ventana actual + margen, y sirve los tiles
/// con acceso de **solo-residentes** (no carga durante el dibujo). El `prefetch` por
/// frame usa el `ChunkCache`; los chunks ausentes devuelven `empty_tile`.
///
/// La fuente de chunks es un **tipo** (`Source` con `load(cx,cy,TileBankBuffer)`), no
/// un puntero a función: `StreamingWorldMap<…, WorldMapChunkLoader>` conoce su backend
/// en compilación. Un `NullChunkSource` (por defecto) no sirve ningún chunk.
///
/// ```text
///   ventana visible (tiles)         StreamingWorldMap                     pool de residentes (Chip RAM)
///   ───────────────────────         ──────────────────                     ─────────────────────────────
///   [x0,x1)×[y0,y1) ──prefetch()─► ChunkCache ──¿residente?─sí─► tile_at() SOLO-residentes
///                                        │  no                            │
///                                        ▼                                └─► tile (vive en el pool)
///                                 Source::load(cx,cy,TileBankBuffer) ─────► escribe el slot
///                                 (tipo en compilación; NullChunkSource = todo Empty)
/// ```
///
/// Ver `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/field/chunk_cache.hpp>
#include <eng/field/tile_source.hpp>

namespace eng::field {

/// Contrato de una fuente de chunks (estático, sin punteros).
template <class Source>
concept ChunkSource = requires(Source& source, eng::s32 cx, eng::s32 cy, eng::TileBankBuffer dst) {
	{ source.load(cx, cy, dst) };
};

/// Fuente vacía: no sirve ningún chunk (todo `Empty`).
struct NullChunkSource {
	[[nodiscard]] LoadResult load(eng::s32, eng::s32, eng::TileBankBuffer) const noexcept {
		return LoadResult::Empty;
	}
};

static_assert(ChunkSource<NullChunkSource>, "NullChunkSource cumple el contrato ChunkSource");

template <eng::u16 ChunkSize = 16, eng::u8 Capacity = 8, ChunkSource Source = NullChunkSource>
class StreamingWorldMap {
public:
	static_assert(ChunkSize != 0u && (ChunkSize & (ChunkSize - 1u)) == 0u,
	              "ChunkSize debe ser potencia de dos");
	static constexpr eng::u32 kCells = static_cast<eng::u32>(ChunkSize) * ChunkSize;
	static constexpr eng::u32 kPoolCells = static_cast<eng::u32>(Capacity) * kCells;

	/// `src` es la fuente de chunks; `pool` el almacén de residentes. En `Empty` las
	/// celdas quedan como `empty_tile`; en `Pending` no se marcan residentes.
	bool init(Source src, eng::TileBankBuffer pool, eng::u16 empty_tile = 0xFFFFu) {
		m_src = src;
		m_empty = empty_tile;
		return m_cache.init(m_loader, pool);
	}

	/// Asegura residentes los chunks que cubren `[x0,x1) x [y0,y1)` (en tiles).
	void prefetch(eng::s32 x0, eng::s32 y0, eng::s32 x1, eng::s32 y1) {
		const eng::s32 size = static_cast<eng::s32>(ChunkSize);
		const eng::s32 cx0 = floor_div(x0, size);
		const eng::s32 cx1 = floor_div(x1 - 1, size);
		const eng::s32 cy0 = floor_div(y0, size);
		const eng::s32 cy1 = floor_div(y1 - 1, size);
		for (eng::s32 cy = cy0; cy <= cy1; ++cy) {
			for (eng::s32 cx = cx0; cx <= cx1; ++cx) {
				(void)m_cache.get(cx, cy);
			}
		}
	}

	eng::u16 tile_at(eng::s32 x, eng::s32 y) const {
		const eng::s32 size = static_cast<eng::s32>(ChunkSize);
		const eng::s32 cx = floor_div(x, size);
		const eng::s32 cy = floor_div(y, size);
		const eng::u16* c = m_cache.find(cx, cy);
		if (c == nullptr) return m_empty;
		const eng::u32 lx = static_cast<eng::u32>(x - cx * size);
		const eng::u32 ly = static_cast<eng::u32>(y - cy * size);
		return c[ly * static_cast<eng::u32>(ChunkSize) + lx];
	}
	constexpr bool is_empty(eng::u16 g) const { return g == m_empty; }

	eng::u32 loads() const { return m_cache.loads(); }
	eng::u32 evictions() const { return m_cache.evictions(); }
	eng::u32 hits() const { return m_cache.hits(); }
	eng::u32 empties() const { return m_cache.empties(); }
	eng::u32 pendings() const { return m_cache.pendings(); }

private:
	/// Adapta la fuente al contrato `ChunkLoader` del `ChunkCache` (referencia al
	/// mapa; el mapa vive más que su caché).
	class MapLoader {
	public:
		constexpr explicit MapLoader(StreamingWorldMap& self) noexcept : m_self(self) {}
		LoadResult load(eng::s32 cx, eng::s32 cy, eng::TileBankBuffer cells) {
			return m_self.load_chunk(cx, cy, cells);
		}

	private:
		StreamingWorldMap& m_self;
	};

	LoadResult load_chunk(eng::s32 cx, eng::s32 cy, eng::TileBankBuffer cells) {
		const LoadResult r = m_src.load(cx, cy, cells);
		if (r == LoadResult::Empty) {
			for (eng::u32 i = 0; i < kCells; ++i) cells[i] = m_empty;
		}
		return r;
	}

	Source m_src {};
	eng::u16 m_empty = 0xFFFFu;
	MapLoader m_loader {*this};
	ChunkCache<ChunkSize, Capacity, MapLoader> m_cache {};
};

} // namespace eng::field
