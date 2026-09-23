#pragma once

/// \file allocator.hpp
/// **Asignadores de bytes** del engine (`eng::util`): el contrato que permite que un
/// contenedor sea de capacidad fija o crezca, sin `malloc` y sin STL.
///
/// El engine distingue dos vidas: `init` (puede reservar y validar) y `frame` (no
/// reserva). Un contenedor que crece recibe un `Allocator`; si le pasas `NullAlloc`,
/// no puede crecer más allá de su capacidad inline. Los asignadores del engine son
/// **de tipo bump** (una arena): no liberan memoria individualmente, solo avanzan un
/// puntero, así que `deallocate` es un no-op y el coste es visible.
///
/// Implementaciones:
/// - `NullAlloc`: nunca asigna (fuerza capacidad fija).
/// - `BumpAlloc`: bump sobre una región que entrega el llamador (`Span<u8>`), sin
///   liberar; la usa el motor de test y quien ya administra su buffer.
/// - `InlineAlloc<N>`: bump sobre un buffer propio del asignador (capacidad `N`).
/// - `ArenaAlloc` (`arena_alloc.hpp`): adaptador sobre `eng::LinearArena`.
///
/// El contrato de dirección es explícito: `allocate(bytes, align)` devuelve una
/// `Span<u8>` vacía si no cabe, y `align` debe ser potencia de dos (normalmente
/// `alignof(T)`).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

/// Contrato de un asignador del engine: entrega bytes alineados y no lanza.
/// `deallocate` puede ser no-op (arenas bump); el contenedor no exige recuperarlos.
template <class A>
concept Allocator = requires(A& a, usize bytes, usize align, Span<u8> block) {
	{ a.allocate(bytes, align) } -> same_as<Span<u8>>;
	a.deallocate(block);
};

/// Asignador nulo: no entrega memoria nunca. Es el defecto de los contenedores que
/// deben quedarse en su capacidad inline.
struct NullAlloc {
	[[nodiscard]] constexpr Span<u8> allocate(usize, usize) noexcept { return {}; }
	constexpr void deallocate(Span<u8>) noexcept {}
};

namespace detail {

/// Reserva bump compartida: avanza `used` dentro de `[base, base + size)`.
/// Devuelve una vista vacía si no cabe. `align` debe ser potencia de dos.
[[nodiscard]] constexpr Span<u8> bump_allocate(u8* base, usize size, usize& used, usize bytes,
					       usize align) noexcept {
	if (bytes == 0u || base == nullptr) {
		return {};
	}
	const uintptr raw = reinterpret_cast<uintptr>(base) + used;
	const uintptr a = align == 0u ? 1u : align;
	const uintptr aligned = eng::align_up_ptr(raw, a);
	const usize padding = static_cast<usize>(aligned - raw);
	if (used + padding + bytes > size) {
		return {};
	}
	used += padding + bytes;
	return Span<u8> {reinterpret_cast<u8*>(aligned), bytes};
}

} // namespace detail

/// Bump sobre una región del llamador. No libera: `clear()` reinicia el offset.
class BumpAlloc {
public:
	constexpr BumpAlloc() noexcept = default;

	explicit constexpr BumpAlloc(Span<u8> region) noexcept
		: m_base(region.data()), m_size(region.size()) {}

	/// Reasocia la región (no libera la anterior; la gobierna el llamador).
	constexpr void reset(Span<u8> region) noexcept {
		m_base = region.data();
		m_size = region.size();
		m_used = 0u;
	}

	/// Reinicia el offset sin tocar el contenido.
	constexpr void clear() noexcept { m_used = 0u; }

	[[nodiscard]] constexpr Span<u8> allocate(usize bytes, usize align) noexcept {
		return detail::bump_allocate(m_base, m_size, m_used, bytes, align);
	}
	constexpr void deallocate(Span<u8>) noexcept {}

	[[nodiscard]] constexpr usize capacity() const noexcept { return m_size; }
	[[nodiscard]] constexpr usize used() const noexcept { return m_used; }
	[[nodiscard]] constexpr usize remaining() const noexcept { return m_size - m_used; }

private:
	u8* m_base = nullptr;
	usize m_size = 0u;
	usize m_used = 0u;
};

/// Bump sobre un buffer propio de `N` bytes (capacidad fija, sin región externa).
/// `allocate` calcula la dirección del buffer en cada llamada, así que copiar o mover
/// el asignador no deja punteros colgando.
template <usize N>
class InlineAlloc {
public:
	constexpr InlineAlloc() noexcept = default;

	constexpr void clear() noexcept { m_used = 0u; }

	[[nodiscard]] constexpr Span<u8> allocate(usize bytes, usize align) noexcept {
		return detail::bump_allocate(m_buf, N, m_used, bytes, align);
	}
	constexpr void deallocate(Span<u8>) noexcept {}

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize used() const noexcept { return m_used; }
	[[nodiscard]] constexpr usize remaining() const noexcept { return N - m_used; }

private:
	alignas(16) u8 m_buf[N] {};
	usize m_used = 0u;
};

} // namespace eng::util
