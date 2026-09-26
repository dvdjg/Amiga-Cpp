#pragma once

/// \file chip_pool.hpp
/// **Pool de bloques de memoria con `free`** (`eng::res::ChipPool`): un asignador *first-fit* sin
/// heap sobre un buffer dado (p. ej. la arena Chip), con fusión de huecos. Complementa la arena
/// *bump* (`LinearArena`, sin `free`) para consumidores que **reciclan** memoria (nametables/CHR
/// que cambian), como un emulador. Es **general** (cualquier juego con buffers reutilizables).
///
/// ```cpp
/// eng::res::ChipPool pool {chip_base, chip_bytes};
/// void* p = pool.alloc(4096);      // primera posición que quepa
/// pool.free(p);                    // devuelve el bloque (y fusiona)
/// pool.free_bytes();               // cuota libre
/// ```

#include <eng/core/types/types.hpp>

namespace eng::res {

/// Asignador de bloques **first-fit** con fusión, sobre un buffer del llamador (sin heap).
class ChipPool {
public:
	static constexpr u8 kMaxBlocks = 32u;

	constexpr ChipPool() = default;
	/// Construye el pool sobre `base` (memoria del llamador), `size` bytes, con alineación `align`.
	constexpr ChipPool(u8* base, u32 size, u32 align = 16u) noexcept
		: m_base(base), m_size(size), m_align(align) {
		if (base != nullptr && size != 0u) {
			m_blocks[0] = Block {0u, size, 0u};
			m_count = 1u;
		}
	}

	/// Reserva `bytes` (alineados a `align`). `nullptr` si no cabe.
	void* alloc(u32 bytes) noexcept {
		if (bytes == 0u || m_base == nullptr) {
			return nullptr;
		}
		const u32 need = align_up(bytes);
		for (u8 i = 0u; i < m_count; ++i) {
			if (m_blocks[i].state != 0u || m_blocks[i].size < need) {
				continue;
			}
			if (m_blocks[i].size > need && m_count < kMaxBlocks) {
				for (u8 j = m_count; j > static_cast<u8>(i + 1u); --j) {
					m_blocks[j] = m_blocks[j - 1u];
				}
				m_blocks[i + 1u] = Block {m_blocks[i].offset + need,
							  m_blocks[i].size - need, 0u};
				++m_count;
				m_blocks[i].size = need;
			}
			m_blocks[i].state = 1u;
			return m_base + m_blocks[i].offset;
		}
		return nullptr;
	}

	/// Libera un bloque de `alloc` (y fusiona huecos contiguos).
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
	[[nodiscard]] u8 block_count() const noexcept { return m_count; }

private:
	struct Block {
		u32 offset = 0u;
		u32 size = 0u;
		u8 state = 0u; ///< 0 = libre, 1 = usado
	};

	/// Redondea `bytes` al alineamiento configurado.
	[[nodiscard]] constexpr u32 align_up(u32 bytes) const noexcept {
		const u32 a = (m_align != 0u) ? m_align : 1u;
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
	u32 m_align = 16u;
	Block m_blocks[kMaxBlocks] {};
	u8 m_count = 0u;
};

} // namespace eng::res
