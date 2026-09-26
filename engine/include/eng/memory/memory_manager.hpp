#pragma once

/// \file memory_manager.hpp
/// **Gestor de memoria central** (`eng::MemoryManager`): reúne la memoria disponible por banco
/// (Chip/Slow/Fast) y sirve reservas por **uso** (política), no por banco explícito. Centraliza el
/// reparto para poder **reasignar con control** y para que el llamador no elija el medio.
///
/// La **política** traduce el uso al medio:
///
/// - `MemUse::Dma` (bitplanes, copperlist, audio, sprites, tiles) → **Chip** (DMA de Agnus).
/// - `MemUse::Compute` (CPU puro: simulación, búsqueda, cómputo de jugadas) → **Fast** si hay.
/// - `MemUse::General` (buffers/tablas sin uso específico) → **Slow** si hay; si no, Fast; si no, Chip.
///
/// Cada banco es un `BlockPool` (reutilizable con `free`). El backend **entrega** los buffers al
/// arrancar (tras sondear `hw::HwInfo`); el juego pide por uso y recibe un `Block<Tag>` con su
/// `MemoryKind`. El medio es un **dato**, no una clase.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/typed.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/arena.hpp>
#include <eng/memory/block_pool.hpp>

namespace eng {

/// Uso de una reserva; determina el banco (política del gestor).
enum class MemUse : u8 {
	Dma,     ///< la consume DMA (bitplanes, copper, audio, sprites, tiles): **Chip**
	Compute, ///< CPU puro (simulación, búsqueda): **Fast** preferido
	General, ///< uso general (buffers/tablas): **Slow** preferido
};

/// Gestor de memoria por uso sobre bancos Chip/Slow/Fast (`BlockPool` cada uno).
class MemoryManager {
public:
	/// Entrega los buffers por banco (del backend). `slow`/`fast` pueden ser nulos (A500 sin ellos).
	bool configure(void* chip, u32 chip_bytes, void* slow, u32 slow_bytes, void* fast,
		       u32 fast_bytes, u32 align = 16u) noexcept {
		m_chip = BlockPool {chip, chip_bytes, MemoryKind::Chip, align};
		m_slow = BlockPool {slow, slow_bytes, MemoryKind::Slow, align};
		m_fast = BlockPool {fast, fast_bytes, MemoryKind::Fast, align};
		m_configured = chip_bytes != 0u;
		return m_configured;
	}
	[[nodiscard]] constexpr bool configured() const noexcept { return m_configured; }

	/// Banco que la política elegiría para `use` (sin tener en cuenta el hueco).
	[[nodiscard]] MemoryKind bank_for(MemUse use) const noexcept {
		switch (use) {
		case MemUse::Dma:
			return MemoryKind::Chip;
		case MemUse::Compute:
			return has_fast() ? MemoryKind::Fast
					  : (has_slow() ? MemoryKind::Slow : MemoryKind::Chip);
		default: // General
			return has_slow() ? MemoryKind::Slow
					  : (has_fast() ? MemoryKind::Fast : MemoryKind::Chip);
		}
	}

	/// Reserva `bytes` para `use`. Prueba el banco preferido y, si no cabe, la cadena de
	/// *fallback* (Dma solo usa Chip; los demás caen a bancos alternativos). `MemoryBlock`
	/// inválido si no cabe en ninguno.
	[[nodiscard]] MemoryBlock allocate(u32 bytes, MemUse use = MemUse::General,
					   u32 alignment = 0u) noexcept {
		const MemoryKind want = bank_for(use);
		MemoryBlock b = try_bank(want, bytes, alignment);
		if (b.valid()) {
			return b;
		}
		// Fallbacks (nunca para Dma: solo Chip ve el DMA de Agnus).
		if (use != MemUse::Dma) {
			const MemoryKind order[3] = {MemoryKind::Fast, MemoryKind::Slow,
						     MemoryKind::Chip};
			for (u8 i = 0u; i < 3u; ++i) {
				const MemoryKind k = order[i];
				if (k != want) {
					b = try_bank(k, bytes, alignment);
					if (b.valid()) {
						return b;
					}
				}
			}
		}
		return {};
	}

	/// Reserva tipada para `use` (mismo `Block<Tag>` que `BlockPool`/`LinearArena`).
	template <class Tag>
	[[nodiscard]] Block<Tag> allocate_block(u32 bytes, MemUse use = MemUse::General,
						u32 alignment = 0u) noexcept {
		const MemoryBlock mb = allocate(bytes, use, alignment);
		return Block<Tag> {mb.buffer<Tag>(), mb.kind};
	}

	/// Devuelve un bloque al banco del que salió (por su `MemoryKind`).
	void free(const MemoryBlock& b) noexcept {
		if (!b.valid()) {
			return;
		}
		switch (b.kind) {
		case MemoryKind::Fast:
			m_fast.free(b.data);
			break;
		case MemoryKind::Slow:
			m_slow.free(b.data);
			break;
		default:
			m_chip.free(b.data);
			break;
		}
	}

	/// Bytes libres del banco dado.
	[[nodiscard]] u32 free_bytes(MemoryKind kind) const noexcept {
		switch (kind) {
		case MemoryKind::Fast:
			return m_fast.free_bytes();
		case MemoryKind::Slow:
			return m_slow.free_bytes();
		default:
			return m_chip.free_bytes();
		}
	}
	/// Hueco libre del banco que la política elegiría para `use`.
	[[nodiscard]] u32 free_bytes(MemUse use) const noexcept { return free_bytes(bank_for(use)); }

	[[nodiscard]] bool has_slow() const noexcept { return m_slow.capacity() != 0u; }
	[[nodiscard]] bool has_fast() const noexcept { return m_fast.capacity() != 0u; }

private:
	/// Intenta reservar en un banco concreto (`MemoryBlock` inválido si no cabe).
	[[nodiscard]] MemoryBlock try_bank(MemoryKind kind, u32 bytes, u32 alignment) noexcept {
		switch (kind) {
		case MemoryKind::Fast:
			return m_fast.allocate(bytes, alignment);
		case MemoryKind::Slow:
			return m_slow.allocate(bytes, alignment);
		default:
			return m_chip.allocate(bytes, alignment);
		}
	}

	BlockPool m_chip {};
	BlockPool m_slow {};
	BlockPool m_fast {};
	bool m_configured = false;
};

} // namespace eng
