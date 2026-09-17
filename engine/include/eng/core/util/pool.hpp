#pragma once

/// \file pool.hpp
/// `eng::util::Pool<T, N>`: pool de objetos de capacidad fija con **handles
/// generacionales**, sin heap.
///
/// Formaliza el patrón que el engine repetía a mano (`ActorStore`, `BackgroundQueue`):
/// un array de `N` slots, un bit de presencia y una free-list de índices. Un `Handle`
/// es `{index, generation}`: al reciclar un slot sube su generación, así que un handle
/// viejo **deja de ser válido** (evita referencias a un objeto que ya no es el que era).
///
/// Coste: alta/baja `O(1)`, sin reservar memoria. `T` debe ser construible por defecto
/// y asignable; `add()` reinicia el slot a `T{}`.
///
/// Uso:
///   eng::util::Pool<Actor, 32> pool;
///   auto h = pool.add();
///   if (Actor* a = pool.get(h)) { a->x = 10; }
///   pool.remove(h);
///   if (!pool.get(h)) { /* handle invalidado */ }

#include <eng/core/types.hpp>
#include <eng/core/util/bitset.hpp>

namespace eng::util {

template <class T, usize N>
class Pool {
	static_assert(N > 0u, "Pool: N debe ser mayor que 0");
	static_assert(N <= 0xfffeu, "Pool: N debe caber en el indice u16");

public:
	/// Referencia estable a un slot. Se invalida al reciclarse el slot (cambia la
	/// generación) o al liberarse explícitamente.
	struct Handle {
		u16 index = 0xffffu;
		u16 generation = 0u;

		[[nodiscard]] constexpr bool valid() const noexcept { return index != 0xffffu; }
		[[nodiscard]] constexpr bool operator==(const Handle& other) const noexcept {
			return index == other.index && generation == other.generation;
		}
		[[nodiscard]] constexpr bool operator!=(const Handle& other) const noexcept {
			return !(*this == other);
		}
	};

	/// Construye la free-list con todos los slots libres.
	constexpr Pool() noexcept { reset(); }

	[[nodiscard]] static constexpr usize capacity() noexcept { return N; }
	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_free_head == 0xffffu; }

	/// Ocupa un slot (lo reinicia a `T{}`). Handle inválido si está lleno.
	constexpr Handle add() noexcept {
		if (m_free_head == 0xffffu) {
			return {};
		}
		const u16 index = m_free_head;
		m_free_head = m_next[index];
		m_items[index] = T {};
		m_used.set(index);
		++m_size;
		return Handle {index, m_generation[index]};
	}

	/// Libera un slot (su handle y los de su generación dejan de ser válidos).
	constexpr bool remove(Handle handle) noexcept {
		if (!valid(handle)) {
			return false;
		}
		m_used.reset(handle.index);
		++m_generation[handle.index];
		m_next[handle.index] = m_free_head;
		m_free_head = handle.index;
		--m_size;
		return true;
	}

	[[nodiscard]] constexpr T* get(Handle handle) noexcept {
		return valid(handle) ? &m_items[handle.index] : nullptr;
	}
	[[nodiscard]] constexpr const T* get(Handle handle) const noexcept {
		return valid(handle) ? &m_items[handle.index] : nullptr;
	}

	[[nodiscard]] constexpr bool valid(Handle handle) const noexcept {
		return handle.valid() && handle.index < N && m_used.test(handle.index) &&
		       m_generation[handle.index] == handle.generation;
	}
	[[nodiscard]] constexpr bool used(u16 index) const noexcept {
		return index < N && m_used.test(index);
	}

	/// Acceso por índice de slot (comprobar `used`); pensado para iterar el parque.
	[[nodiscard]] constexpr T& at(u16 index) noexcept { return m_items[index]; }
	[[nodiscard]] constexpr const T& at(u16 index) const noexcept { return m_items[index]; }

	/// Handle (con generación) del slot, o inválido si está libre.
	[[nodiscard]] constexpr Handle handle_at(u16 index) const noexcept {
		return used(index) ? Handle {index, m_generation[index]} : Handle {};
	}

	/// Vacía el pool y sube la generación de todos los slots: los handles anteriores
	/// quedan permanentemente inválidos (no pueden "resucitar" al reutilizar el índice).
	constexpr void reset() noexcept {
		for (u16 i = 0; i < static_cast<u16>(N); ++i) {
			m_next[i] = static_cast<u16>(i + 1u);
			++m_generation[i];
		}
		m_next[N - 1u] = 0xffffu;
		m_free_head = 0u;
		m_used.reset();
		m_size = 0u;
	}

	/// Recorre los slots vivos: `fn(T&, Handle)`.
	template <class Fn>
	constexpr void for_each(Fn fn) {
		for (u16 i = 0; i < static_cast<u16>(N); ++i) {
			if (m_used.test(i)) {
				fn(m_items[i], Handle {i, m_generation[i]});
			}
		}
	}
	template <class Fn>
	constexpr void for_each(Fn fn) const {
		for (u16 i = 0; i < static_cast<u16>(N); ++i) {
			if (m_used.test(i)) {
				fn(m_items[i], Handle {i, m_generation[i]});
			}
		}
	}

private:
	T m_items[N] {};
	u16 m_generation[N] {};
	u16 m_next[N] {};
	BitSet<N> m_used {};
	u16 m_free_head = 0u;
	u16 m_size = 0u;
};

} // namespace eng::util
