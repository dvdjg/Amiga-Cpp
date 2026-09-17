#pragma once

/// \file arena_alloc.hpp
/// `eng::util::ArenaAlloc`: adapta el `LinearArena` del engine (`eng/memory/arena.hpp`)
/// al contrato `Allocator` (`allocator.hpp`), para que los contenedores de `eng::util`
/// crezcan sobre la memoria que el backend ya administra.
///
/// Es la pieza que conecta la librería de utilidades con el modelo de memoria: un
/// `Vector<T, ArenaAlloc>` reserva de la arena (bump) durante `init`/carga y **no**
/// reserva en `frame`. La arena no libera bloques individuales, así que `deallocate`
/// es un no-op: la memoria se recupera con `LinearArena::clear()` al terminar la fase.
///
/// Uso:
///   eng::LinearArena arena {chip, bytes, eng::MemoryKind::Any};
///   eng::util::ArenaAlloc alloc {arena};
///   eng::util::Vector<eng::u32, eng::util::ArenaAlloc> ids {alloc};

#include <eng/core/util/allocator.hpp>
#include <eng/memory/arena.hpp>

namespace eng::util {

/// Asignador sobre una `LinearArena` (no propietario). Si no hay arena asociada,
/// `allocate` devuelve vacío (como `NullAlloc`).
class ArenaAlloc {
public:
	constexpr ArenaAlloc() noexcept = default;
	explicit constexpr ArenaAlloc(eng::LinearArena& arena) noexcept : m_arena(&arena) {}

	/// Asocia la arena sobre la que reservar (no la posee).
	constexpr void attach(eng::LinearArena& arena) noexcept { m_arena = &arena; }
	[[nodiscard]] constexpr bool valid() const noexcept { return m_arena != nullptr; }

	[[nodiscard]] Span<u8> allocate(usize bytes, usize align) noexcept {
		if (m_arena == nullptr || bytes == 0u) {
			return {};
		}
		const eng::MemoryBlock block =
			m_arena->allocate(static_cast<eng::u32>(bytes),
					  align == 0u ? 1u : static_cast<eng::u32>(align));
		if (!block.valid()) {
			return {};
		}
		return Span<u8> {static_cast<u8*>(block.data), static_cast<usize>(block.size)};
	}
	constexpr void deallocate(Span<u8>) noexcept {}

private:
	eng::LinearArena* m_arena = nullptr;
};

} // namespace eng::util
