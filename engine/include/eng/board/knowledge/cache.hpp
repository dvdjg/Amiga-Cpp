#pragma once

/// \file cache.hpp
/// **Caché LRU de bloques** sobre una `BlockSource`: guarda los últimos `Capacity`
/// bloques leídos para no volver a pedirlos (cada petición puede costar un *seek*
/// de disquete de ~200 ms). Se apoya en `eng::util::LruCache` (HOST-127).
///
/// La fuente es un **parámetro de plantilla** (`Source` con el concepto
/// `BlockSource`), no un puntero a función: `BlockCache<RamBlockSource, 256, 8>`
/// conoce su backend en compilación y no hay despacho inseguro. La RAM que ocupa es
/// exactamente `Capacity * BlockSize` (inline, sin heap), que el `MemoryPlan` fija
/// por perfil.
///
/// Verificación: HOST-146.

#include <eng/board/storage/block_source.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/array.hpp>
#include <eng/core/util/lru_cache.hpp>

namespace eng::board {

template <BlockSource Source, u32 BlockSize, u32 Capacity>
class BlockCache {
	static_assert(BlockSize > 0u, "BlockCache: BlockSize > 0");
	static_assert(Capacity > 0u, "BlockCache: Capacity > 0");

public:
	static constexpr u32 block_size = BlockSize;
	static constexpr u32 capacity = Capacity;

	constexpr explicit BlockCache(const Source& source) noexcept : m_source(source) {}

	/// Devuelve la vista del bloque (válida hasta el siguiente `get`). Vacía si el
	/// bloque no existe (`Empty`) o no está listo (`Pending`).
	[[nodiscard]] eng::Span<const u8> get(u32 block_id) {
		if (Slot* hit = m_cache.get(block_id)) {
			++m_hits;
			return hit->data.span().as_const();
		}
		++m_misses;

		const bool evicting = m_cache.full();
		if (!m_cache.put(block_id, Slot {})) {
			Slot* existing = m_cache.get(block_id);
			return existing != nullptr ? existing->data.span().as_const() : eng::Span<const u8> {};
		}
		if (evicting) {
			++m_evictions;
		}

		Slot* slot = m_cache.get(block_id);
		if (slot == nullptr) {
			return eng::Span<const u8> {};
		}
		const BlockStatus status = m_source.fetch(block_id, slot->data.span());
		if (status != BlockStatus::Ready) {
			m_cache.erase(block_id); // no cachear ausentes ni pendientes
			return eng::Span<const u8> {};
		}
		return slot->data.span().as_const();
	}

	[[nodiscard]] u32 hits() const noexcept { return m_hits; }
	[[nodiscard]] u32 misses() const noexcept { return m_misses; }
	[[nodiscard]] u32 evictions() const noexcept { return m_evictions; }
	[[nodiscard]] u32 resident() const noexcept { return static_cast<u32>(m_cache.size()); }
	void reset_stats() noexcept {
		m_hits = 0u;
		m_misses = 0u;
		m_evictions = 0u;
	}

private:
	struct Slot {
		eng::util::Array<u8, BlockSize> data {};
	};

	/// Referencia a la fuente (vive más que la caché); FileBlockSource no es copiable.
	const Source& m_source;
	eng::util::LruCache<u32, Slot, Capacity> m_cache {};
	u32 m_hits = 0u;
	u32 m_misses = 0u;
	u32 m_evictions = 0u;
};

} // namespace eng::board
