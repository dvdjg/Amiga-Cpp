#pragma once

/// \file mem_bank.hpp
/// **Banco de memoria tipado** (`eng::MemBank<Bank>`): una especialización por banco
/// (`MemoryKind::Chip`/`Slow`/`Fast`) que entrega reservas **ya tipadas por el banco** — un
/// `TypedBlock<Tag, Bank>` con una `Address<Bank>` que **no convive** con las de otros bancos.
///
/// Así una API que exige Chip RAM (DMA de Agnus: bitplanes, copper, audio, sprites) acepta
/// `Address<Chip>` y **no compila** si le pasas `Address<Fast>`. El banco viaja en el **tipo**
/// (etiqueta vacía, coste cero); no hay nombres de caso concreto ni comprobaciones en runtime.
///
/// ```cpp
/// eng::MemBank<eng::MemoryKind::Chip> chip;
/// chip.configure(chip_base, chip_bytes);
/// eng::TypedBlock<eng::PlaneTag, eng::MemoryKind::Chip> planes = chip.reserve<eng::PlaneTag>(n);
/// eng::Address<eng::MemoryKind::Chip> dma = planes.address();   // solo esto es DMA
/// ```
///
/// El banco **se conoce en runtime** (los tamaños se fijan en el setup), pero eso no impide el
/// tipado: creas las tres instancias y las que no tengan bytes (p. ej. Fast/Slow en un A500)
/// simplemente devuelven bloques inválidos. `reserve` es miembro del banco, no del bloque.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/block_pool.hpp>

namespace eng {

/// Bloque tipado por dominio (`Tag`) **y** banco (`K`): la vista y su dirección son coherentes.
template <class Tag, MemoryKind K>
struct TypedBlock {
	Bytes<Tag> view {};
	[[nodiscard]] constexpr bool valid() const noexcept { return !view.empty(); }
	[[nodiscard]] constexpr Address<K> address() const noexcept {
		return Address<K> {reinterpret_cast<eng::uintptr>(view.data())};
	}
	[[nodiscard]] constexpr eng::u8* data() const noexcept { return view.data(); }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return view.size(); }
};

/// **Banco de memoria** de un `MemoryKind` concreto. Posee un `BlockPool` de su banco y entrega
/// `TypedBlock<Tag, K>`. Sin bytes asignados (banco ausente) devuelve bloques inválidos.
template <MemoryKind K>
class MemBank {
public:
	constexpr MemBank() = default;

	/// Asocia el buffer del banco (lo entrega el backend tras sondear el hardware). `size == 0`
	/// deja el banco vacío (p. ej. Fast/Slow en un A500).
	constexpr void configure(void* base, u32 size, u32 align = 16u) noexcept {
		m_pool = BlockPool {base, size, K, align};
	}

	/// Reserva `bytes` (alineados) como `TypedBlock<Tag, K>`. Inválido si no cabe o el banco
	/// está vacío.
	template <class Tag>
	[[nodiscard]] TypedBlock<Tag, K> reserve(u32 bytes, u32 alignment = 0u) noexcept {
		const MemoryBlock mb = m_pool.allocate(bytes, alignment);
		return TypedBlock<Tag, K> {mb.buffer<Tag>()};
	}

	/// Devuelve un bloque al banco (cualquier dominio `Tag` del mismo banco).
	template <class Tag>
	void release(const TypedBlock<Tag, K>& block) noexcept {
		m_pool.free(block.data());
	}
	void release(const void* ptr) noexcept { m_pool.free(const_cast<void*>(ptr)); }

	[[nodiscard]] constexpr u32 free_bytes() const noexcept { return m_pool.free_bytes(); }
	[[nodiscard]] constexpr u32 capacity() const noexcept { return m_pool.capacity(); }
	[[nodiscard]] constexpr MemoryKind kind() const noexcept { return K; }
	/// Acceso sin tipo al pool subyacente (para la política del gestor o para buffers crudos).
	[[nodiscard]] constexpr BlockPool& pool() noexcept { return m_pool; }
	[[nodiscard]] constexpr const BlockPool& pool() const noexcept { return m_pool; }

private:
	BlockPool m_pool {};
};

} // namespace eng
