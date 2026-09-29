#pragma once

/// \file mem_bank.hpp
/// **Banco de memoria tipado** (`eng::MemBank<Bank>`): una especialización por banco
/// (`MemoryKind::Chip`/`Slow`/`Fast`) que entrega reservas **ya tipadas por el banco** — un
/// `Block<Tag, Bank>` con una `Address<Bank>` que **no convive** con las de otros bancos.
///
/// Así una API que exige Chip RAM (DMA de Agnus: bitplanes, copper, audio, sprites) acepta
/// `Address<Chip>` y **no compila** si le pasas `Address<Fast>`. El banco viaja en el **tipo**
/// (etiqueta vacía, coste cero); no hay nombres de caso concreto ni comprobaciones en runtime.
///
/// ```cpp
/// eng::MemBank<eng::MemoryKind::Chip> chip;
/// chip.configure(chip_base, chip_bytes);
/// eng::Block<eng::PlaneTag, eng::MemoryKind::Chip> planes = chip.reserve<eng::PlaneTag>(n);
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

/// **Banco de memoria** de un `MemoryKind` concreto. Posee un `BlockPool` de su banco y entrega
/// `Block<Tag, K>` (`Block<Tag, K>`, el mismo tipo). Sin bytes asignados (banco ausente)
/// los bloques salen inválidos.
template <MemoryKind K>
class MemBank {
public:
	constexpr MemBank() = default;

	/// Asocia el buffer del banco (lo entrega el backend tras sondear el hardware). `size == 0`
	/// deja el banco vacío (p. ej. Fast/Slow en un A500).
	constexpr void configure(void* base, u32 size, u32 align = 16u) noexcept {
		m_pool = BlockPool {base, size, K, align};
	}

	/// Asocia el banco a una **`LinearArena` de respaldo**: el pool **delega** en ella (mismo
	/// buffer y cursor), de modo que una arena y un banco que comparten buffer no se solapan. Es
	/// lo que usa `MemoryManager::configure_backing` (`INTERNAL_TYPE_SYSTEM.md` §3.6).
	constexpr void configure_backing(LinearArena& arena) noexcept {
		m_pool.configure_backing(arena);
	}

	/// Reserva `bytes` (alineados) como `Block<Tag, K>`. Inválido si no cabe o el banco
	/// está vacío.
	template <class Tag>
	[[nodiscard]] Block<Tag, K> reserve(u32 bytes, u32 alignment = 0u) noexcept {
		const MemoryBlock mb = m_pool.allocate(bytes, alignment);
		return Block<Tag, K> {mb.buffer<Tag>(), K};
	}

	/// **Devuelve un bloque al banco** (cualquier dominio `Tag` del mismo banco). El `Block` no
	/// queda invalidado por sí solo (es un valor): el llamador lo descarta. La **liberación
	/// ordenada** (antes de reutilizar/liberar un búfer con DMA pendiente) es responsabilidad del
	/// flujo; ver `MEMORY_OWNERSHIP.md` §"Contrato del developer".
	template <class Tag>
	void release(const Block<Tag, K>& block) noexcept {
		m_pool.free(block.data());
		block.invalidate(); // diagnóstico: usar sus vistas después trapa
	}
	void release(const void* ptr) noexcept { m_pool.free(const_cast<void*>(ptr)); }

	[[nodiscard]] constexpr u32 free_bytes() const noexcept { return m_pool.free_bytes(); }
	/// Bytes **en uso** (suma de bloques vivos).
	[[nodiscard]] constexpr u32 used_bytes() const noexcept { return m_pool.used_bytes(); }
	/// **Pico** de uso desde el arranque (presupuesto): máximo histórico de Bytes vivos.
	[[nodiscard]] constexpr u32 peak_bytes() const noexcept { return m_pool.peak_bytes(); }
	/// Capacidad total de cada banco.
	[[nodiscard]] constexpr u32 capacity() const noexcept { return m_pool.capacity(); }
	/// Foto del banco para telemetría (mismo tipo que la de la arena).
	[[nodiscard]] constexpr ArenaSnapshot snapshot() const noexcept {
		return ArenaSnapshot {0u, m_pool.capacity(), m_pool.used_bytes(), m_pool.peak_bytes(),
				      m_pool.free_bytes(), K};
	}
	[[nodiscard]] constexpr MemoryKind kind() const noexcept { return K; }
	/// Bloques (slots) que lleva el pool (reservas libres + usadas): diagnóstico.
	[[nodiscard]] constexpr u16 block_count() const noexcept { return m_pool.block_count(); }

	/// **Causa de fallo** de una reserva (diagnóstico sin punteros). `Ok` si hay capacidad; si no,
	/// distingue banco ausente / sin capacidad / fragmentación de slots.
	enum class Status : eng::u8 { Ok, BankAbsent, NoSpace, Fragmented };

	/// **Estado del banco** para diagnóstico: `BankAbsent` si no tiene buffer; `Fragmented` si el
	/// pool agotó los slots (sube `kMaxSlots`); `NoSpace` si no queda hueco; `Ok` en otro caso.
	[[nodiscard]] constexpr Status status() const noexcept {
		if (m_pool.capacity() == 0u) {
			return Status::BankAbsent;
		}
		if (m_pool.slots_left() == 0u) {
			return Status::Fragmented;
		}
		return m_pool.free_bytes() == 0u ? Status::NoSpace : Status::Ok;
	}
	/// Nombre legible de `status()` (para overlays/logs; sin punteros).
	[[nodiscard]] static constexpr const char* status_name(Status s) noexcept {
		switch (s) {
			case Status::Ok: return "ok";
			case Status::BankAbsent: return "absent";
			case Status::NoSpace: return "full";
			case Status::Fragmented: return "fragmented";
		}
		return "?";
	}
	/// Acceso sin tipo al pool subyacente (para la política del gestor o para buffers crudos).
	[[nodiscard]] constexpr BlockPool& pool() noexcept { return m_pool; }
	[[nodiscard]] constexpr const BlockPool& pool() const noexcept { return m_pool; }

private:
	BlockPool m_pool {};
};

} // namespace eng
