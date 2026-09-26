#pragma once

/// \file block_pool.hpp
/// **Pool de bloques con `free`** (`eng::BlockPool`): asignador *first-fit* sin heap sobre un
/// buffer dado, con fusión de huecos. Complementa la arena *bump* (`LinearArena`, sin `free`) para
/// quien **recicla** memoria. **No** está atado a un tipo de memoria: opera sobre cualquier buffer
/// y lleva su `MemoryKind` (Chip/Fast/Slow/Any) en cada reserva, igual que la arena.
///
/// Es **genérico** (cualquier medio y cualquier consumidor): el **medio** es un dato, no una
/// especialización. Devuelve `MemoryBlock`/`Block<Tag>` como `LinearArena`.
///
/// ```cpp
/// eng::BlockPool pool {chip_base, chip_bytes, eng::MemoryKind::Chip};
/// auto b = pool.allocate_block<eng::PlaneTag>(4096u);
/// pool.free(b.view.data());
/// ```

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/arena.hpp>

namespace eng {

/// Asignador de bloques **first-fit** con fusión, sobre un buffer del llamador (sin heap).
class BlockPool {
public:
	static constexpr u8 kMaxBlocks = 32u;

	constexpr BlockPool() = default;
	/// Construye el pool sobre `base` (memoria del llamador), `size` bytes, con medio `kind` y
	/// alineación por defecto `align`.
	constexpr BlockPool(void* base, u32 size, MemoryKind kind = MemoryKind::Any,
			    u32 align = 16u) noexcept
		: m_base(static_cast<u8*>(base)), m_size(size), m_kind(kind), m_align(align) {
		if (base != nullptr && size != 0u) {
			m_blocks[0] = Slot {0u, size, 0u};
			m_count = 1u;
		}
	}

	/// Reserva `bytes` alineados a `alignment` (0 = la alineación por defecto del pool).
	/// `MemoryBlock` inválido si no cabe.
	MemoryBlock allocate(u32 bytes, u32 alignment = 0u) noexcept {
		if (bytes == 0u || m_base == nullptr) {
			return {};
		}
		const u32 need = align_up(bytes, alignment != 0u ? alignment : m_align);
		for (u8 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].state != 0u || m_blocks[i].size < need) {
				continue;
			}
			if (m_blocks[i].size > need && m_count < kMaxBlocks) {
				for (u8 j = m_count; j > static_cast<u8>(i + 1u); --j) {
					m_blocks[j] = m_blocks[j - 1u];
				}
				m_blocks[i + 1u] = Slot {m_blocks[i].offset + need,
							  m_blocks[i].size - need, 0u};
				++m_count;
				m_blocks[i].size = need;
			}
			m_blocks[i].state = 1u;
			return MemoryBlock {m_base + m_blocks[i].offset, need, m_kind};
		}
		return {};
	}

	/// Reserva tipada (como `LinearArena::allocate_block`): `Block<Tag>` con el medio del pool.
	template <class Tag>
	[[nodiscard]] Block<Tag> allocate_block(u32 bytes, u32 alignment = 0u) noexcept {
		const MemoryBlock mb = allocate(bytes, alignment);
		return Block<Tag> {mb.buffer<Tag>(), mb.kind};
	}

	/// Libera un bloque de `allocate` (y fusiona huecos contiguos).
	void free(void* ptr) noexcept {
		if (ptr == nullptr || m_base == nullptr) {
			return;
		}
		const u32 off = static_cast<u32>(static_cast<u8*>(ptr) - m_base);
		for (u8 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].offset == off && m_blocks[i].state == 1u) {
				m_blocks[i].state = 0u;
				coalesce();
				return;
			}
		}
	}

	/// Bytes libres (suma de bloques libres).
	[[nodiscard]] u32 free_bytes() const noexcept {
		u32 t = 0u;
		for (u8 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].state == 0u) {
				t += m_blocks[i].size;
			}
		}
		return t;
	}
	[[nodiscard]] u32 capacity() const noexcept { return m_size; }
	[[nodiscard]] MemoryKind kind() const noexcept { return m_kind; }
	[[nodiscard]] u8 block_count() const noexcept { return m_count; }

private:
	struct Slot {
		u32 offset = 0u;
		u32 size = 0u;
		u8 state = 0u; ///< 0 = libre, 1 = usado
	};

	/// Redondea `bytes` al alineamiento dado.
	[[nodiscard]] static constexpr u32 align_up(u32 bytes, u32 alignment) noexcept {
		const u32 a = (alignment != 0u) ? alignment : 1u;
		return (bytes + a - 1u) & ~(a - 1u);
	}
	/// Fusiona bloques libres contiguos (tras `free`).
	void coalesce() noexcept {
		for (u8 i = 0u; i + 1u < m_count;) {
			if (m_blocks[i].state == 0u && m_blocks[i + 1u].state == 0u) {
				m_blocks[i].size += m_blocks[i + 1u].size;
				for (u8 j = static_cast<u8>(i + 1u); j + 1u < m_count; ++j) {
					m_blocks[j] = m_blocks[j + 1u];
				}
				--m_count;
			} else {
				++i;
			}
		}
	}

	u8* m_base = nullptr;
	u32 m_size = 0u;
	MemoryKind m_kind = MemoryKind::Any;
	u32 m_align = 16u;
	Slot m_blocks[kMaxBlocks] {};
	u8 m_count = 0u;
};

} // namespace eng
