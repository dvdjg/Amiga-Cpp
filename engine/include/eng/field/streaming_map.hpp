#pragma once

/// \file streaming_map.hpp
/// `WorldMap` disperso con **streaming**: mantiene residentes (en un pool de Chip RAM
/// del llamador) los chunks que cubren la ventana actual + margen, y sirve los tiles
/// con acceso de **solo-residentes** (no carga durante el dibujo). El `prefetch` por
/// frame usa el `ChunkCache`; los chunks ausentes devuelven `empty_tile`.
///
/// Ver `docs/engine/architecture/CONTENT_AND_TILEMAP.md` §2.

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/field/chunk_cache.hpp>
#include <eng/field/tile_source.hpp>

namespace eng::field {

template <eng::u16 ChunkSize = 16, eng::u8 Capacity = 8>
class StreamingWorldMap {
public:
	static_assert(ChunkSize != 0u && (ChunkSize & (ChunkSize - 1u)) == 0u,
	              "ChunkSize debe ser potencia de dos");
	static constexpr eng::u32 kCells = static_cast<eng::u32>(ChunkSize) * ChunkSize;
	static constexpr eng::u32 kPoolCells = static_cast<eng::u32>(Capacity) * kCells;

	/// Fuente de chunks: rellena `cells` (kCells) y devuelve el resultado de la
	/// carga (`Ready`/`Empty`/`Pending`). En `Empty` las celdas quedan como
	/// `empty_tile`; en `Pending` no se marcan residentes (se reintenta).
	struct Source {
		LoadResult (*load)(void* user, eng::s32 cx, eng::s32 cy, eng::u16* cells) = nullptr;
		void* user = nullptr;
	};

	bool init(Source src, eng::Span<eng::u16> pool, eng::u16 empty_tile = 0xFFFFu) {
		m_src = src;
		m_empty = empty_tile;
		return m_cache.init({ &StreamingWorldMap::load_trampoline, this }, pool);
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
	static LoadResult load_trampoline(void* user, eng::s32 cx, eng::s32 cy, eng::u16* cells) {
		auto* self = static_cast<StreamingWorldMap*>(user);
		if (self->m_src.load == nullptr) {
			for (eng::u32 i = 0; i < kCells; ++i) cells[i] = self->m_empty;
			return LoadResult::Empty;
		}
		const LoadResult r = self->m_src.load(self->m_src.user, cx, cy, cells);
		if (r == LoadResult::Empty) {
			for (eng::u32 i = 0; i < kCells; ++i) cells[i] = self->m_empty;
		}
		return r;
	}

	ChunkCache<ChunkSize, Capacity> m_cache {};
	Source m_src {};
	eng::u16 m_empty = 0xFFFFu;
};

} // namespace eng::field
