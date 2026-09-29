#pragma once

/// \file budget.hpp
/// **Presupuesto de memoria del juego**: vista de solo lectura sobre los **bancos** del engine
/// (`MemoryManager`) para decidir si un recurso **cabe antes de pedirlo**, en vez de descubrirlo
/// por un fallo de reserva. Es la mitad de `app.resources()` (`PUBLIC_GAME_API.md` §2.1.4); la
/// caché de assets y `res::load<T>` se construyen encima (`RESOURCE_SYSTEM.md`).
///
/// ```cpp
/// if (app.resources().can_fit_chip(spr_bytes)) { /* cargar el sprite */ }
/// overlay.bytes(app.resources().used_chip(), app.resources().capacity_chip());
/// ```
///
/// No posee memoria (observa la del backend) y no reserva nada.

#include <eng/core/types/memory_kind.hpp>
#include <eng/core/types/ptr.hpp>
#include <eng/core/types/types.hpp>
#include <eng/memory/memory_manager.hpp>

namespace eng::res {

/// Vista de presupuesto de los bancos Chip/Slow/Fast de un `MemoryManager`. `used`/`capacity`
/// salen del **pool del banco** (reservas reales); `frame` (scratch) no se mide aquí.
class Budget {
public:
	explicit constexpr Budget(eng::Ref<eng::MemoryManager> memory = {}) noexcept
		: m_memory(memory) {}

	[[nodiscard]] constexpr bool valid() const noexcept { return m_memory.valid(); }

	/// Bytes **usados** de cada banco.
	[[nodiscard]] constexpr u32 used_chip() const noexcept { return chip().used_bytes(); }
	[[nodiscard]] constexpr u32 used_slow() const noexcept { return slow().used_bytes(); }
	[[nodiscard]] constexpr u32 used_fast() const noexcept { return fast().used_bytes(); }
	/// Bytes **libres** de cada banco.
	[[nodiscard]] constexpr u32 remaining_chip() const noexcept { return chip().free_bytes(); }
	[[nodiscard]] constexpr u32 remaining_slow() const noexcept { return slow().free_bytes(); }
	[[nodiscard]] constexpr u32 remaining_fast() const noexcept { return fast().free_bytes(); }
	/// Capacidad total de cada banco.
	[[nodiscard]] constexpr u32 capacity_chip() const noexcept { return chip().capacity(); }
	[[nodiscard]] constexpr u32 capacity_slow() const noexcept { return slow().capacity(); }
	[[nodiscard]] constexpr u32 capacity_fast() const noexcept { return fast().capacity(); }

	/// `true` si `bytes` caben en el banco del medio indicado (permite **rechazar por
	/// presupuesto** en vez de dejar que la reserva falle). `Fast` sin banco cae a Slow (como
	/// `fast_or_slow`); `Any`/`Chip` van a Chip.
	[[nodiscard]] constexpr bool can_fit(u32 bytes, MemoryKind kind) const noexcept {
		switch (kind) {
			case MemoryKind::Fast:
				return (m_memory->fast().capacity() != 0u ? remaining_fast()
									  : remaining_slow()) >= bytes;
			case MemoryKind::Slow:
				return remaining_slow() >= bytes;
			default:
				return remaining_chip() >= bytes;
		}
	}
	[[nodiscard]] constexpr bool can_fit_chip(u32 bytes) const noexcept {
		return remaining_chip() >= bytes;
	}
	[[nodiscard]] constexpr bool can_fit_slow(u32 bytes) const noexcept {
		return remaining_slow() >= bytes;
	}

	/// El `MemoryManager` observado (para reservar o para telemetría).
	[[nodiscard]] constexpr eng::MemoryManager& memory() const noexcept { return *m_memory; }

private:
	/// Banco Chip observado.
	[[nodiscard]] constexpr const eng::MemBank<MemoryKind::Chip>& chip() const noexcept {
		return m_memory->chip();
	}
	/// Banco Slow observado.
	[[nodiscard]] constexpr const eng::MemBank<MemoryKind::Slow>& slow() const noexcept {
		return m_memory->slow();
	}
	/// Banco Fast observado.
	[[nodiscard]] constexpr const eng::MemBank<MemoryKind::Fast>& fast() const noexcept {
		return m_memory->fast();
	}

	eng::Ref<eng::MemoryManager> m_memory {};
};

} // namespace eng::res
