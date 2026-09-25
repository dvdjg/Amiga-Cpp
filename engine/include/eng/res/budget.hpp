#pragma once

/// \file budget.hpp
/// **Presupuesto de memoria del juego**: vista de solo lectura sobre las arenas del engine
/// (`MemorySystem`) para decidir si un recurso **cabe antes de pedirlo**, en vez de
/// descubrirlo por un fallo de reserva. Es la mitad de `app.resources()`
/// (`PUBLIC_GAME_API.md` §2.1.4); la caché de assets y `res::load<T>` se construyen
/// encima (`RESOURCE_SYSTEM.md`).
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
#include <eng/memory/arena.hpp>

namespace eng::res {

/// Vista de presupuesto de las tres arenas base (Chip/Slow/Frame) de un `MemorySystem`.
class Budget {
public:
	explicit constexpr Budget(eng::Ref<eng::MemorySystem> memory = {}) noexcept
		: m_memory(memory) {}

	[[nodiscard]] constexpr bool valid() const noexcept { return m_memory.valid(); }

	/// Bytes **usados** de cada arena.
	[[nodiscard]] constexpr u32 used_chip() const noexcept { return chip().used(); }
	[[nodiscard]] constexpr u32 used_slow() const noexcept { return slow().used(); }
	[[nodiscard]] constexpr u32 used_frame() const noexcept { return frame().used(); }
	/// Bytes **libres** de cada arena.
	[[nodiscard]] constexpr u32 remaining_chip() const noexcept { return chip().remaining(); }
	[[nodiscard]] constexpr u32 remaining_slow() const noexcept { return slow().remaining(); }
	[[nodiscard]] constexpr u32 remaining_frame() const noexcept { return frame().remaining(); }
	/// Capacidad total de cada arena.
	[[nodiscard]] constexpr u32 capacity_chip() const noexcept { return chip().capacity(); }
	[[nodiscard]] constexpr u32 capacity_slow() const noexcept { return slow().capacity(); }
	[[nodiscard]] constexpr u32 capacity_frame() const noexcept { return frame().capacity(); }

	/// `true` si `bytes` caben en la arena indicada: permite **rechazar por presupuesto**
	/// (degradar el efecto) en vez de dejar que la reserva falle. `Fast` se sirve de la
	/// arena `slow` (el engine no separa una arena Fast propia); `Any`/`Chip` de Chip.
	[[nodiscard]] constexpr bool can_fit(u32 bytes, MemoryKind kind) const noexcept {
		return arena_for(kind).remaining() >= bytes;
	}
	[[nodiscard]] constexpr bool can_fit_chip(u32 bytes) const noexcept {
		return remaining_chip() >= bytes;
	}
	[[nodiscard]] constexpr bool can_fit_slow(u32 bytes) const noexcept {
		return remaining_slow() >= bytes;
	}

	/// El `MemorySystem` observado (para reservar o para un `snapshot()` de telemetría).
	[[nodiscard]] constexpr eng::MemorySystem& memory() const noexcept { return *m_memory; }

private:
	[[nodiscard]] constexpr const LinearArena& chip() const noexcept { return m_memory->chip; }
	[[nodiscard]] constexpr const LinearArena& slow() const noexcept { return m_memory->slow; }
	[[nodiscard]] constexpr const LinearArena& frame() const noexcept { return m_memory->frame; }
	[[nodiscard]] constexpr const LinearArena& arena_for(MemoryKind kind) const noexcept {
		switch (kind) {
			case MemoryKind::Slow:
			case MemoryKind::Fast:
				return slow();
			default:
				return chip();
		}
	}

	eng::Ref<eng::MemorySystem> m_memory {};
};

} // namespace eng::res
