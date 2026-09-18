#pragma once

/// \file cache.hpp
/// **Caché LRU de bloques** sobre una `BlockSource`: guarda los últimos `Capacity`
/// bloques leídos para no volver a pedirlos (cada petición puede costar un *seek*
/// de disquete de ~200 ms). Se apoya en `eng::util::LruCache` (HOST-127).
///
/// La RAM que ocupa es exactamente `Capacity * BlockSize` (inline, sin heap), que
/// el `MemoryPlan` fija por perfil: 4–8 kB en `P20`, 32–64 kB en `P512+`.
///
/// Verificación: HOST-146.

#include <eng/board/storage/block_source.hpp>
#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/lru_cache.hpp>

namespace eng::board {

template <u32 BlockSize, u32 Capacity>
class BlockCache {
	static_assert(BlockSize > 0u, "BlockCache: BlockSize > 0");
	static_assert(Capacity > 0u, "BlockCache: Capacity > 0");

public:
	static constexpr u32 block_size = BlockSize;
	static constexpr u32 capacity = Capacity;

	explicit BlockCache(const BlockSource& source) noexcept : m_source(source) {}

	/// Devuelve la vista del bloque (válida hasta el siguiente `get`). Vacía si el
	/// bloque no existe (`Empty`) o no está listo (`Pending`).
	[[nodiscard]] eng::Span<const u8> get(u32 block_id) {
		if (Slot* hit = m_cache.get(block_id)) {
			++m_hits;
			return eng::Span<const u8> {hit->data, BlockSize};
		}
		++m_misses;

		const bool evicting = m_cache.full();
		if (!m_cache.put(block_id, Slot {})) {
			// Clave ya presente (no esperado tras un fallo de caché): reutilízala.
			Slot* existing = m_cache.get(block_id);
			return existing != nullptr ? eng::Span<const u8> {existing->data, BlockSize}
			                           : eng::Span<const u8> {};
		}
		if (evicting) {
			++m_evictions;
		}

		Slot* slot = m_cache.get(block_id);
		if (slot == nullptr) {
			return eng::Span<const u8> {};
		}
		const BlockStatus status = m_source.fetch(block_id, eng::Span<u8> {slot->data, BlockSize});
		if (status != BlockStatus::Ready) {
			m_cache.erase(block_id); // no cachear ausentes ni pendientes
			return eng::Span<const u8> {};
		}
		return eng::Span<const u8> {slot->data, BlockSize};
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
		u8 data[BlockSize] {};
	};

	BlockSource m_source {};
	eng::util::LruCache<u32, Slot, Capacity> m_cache {};
	u32 m_hits = 0u;
	u32 m_misses = 0u;
	u32 m_evictions = 0u;
};

} // namespace eng::board
