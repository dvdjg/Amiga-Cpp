#pragma once

/// \file sparse_set.hpp
/// `eng::util::SparseSet<T, MaxElements>`: conjunto **disperso-denso** con elementos
/// identificados por un índice `u16` en `[0, MaxElements)`. `contains`/`find`/`insert`/
/// `erase` son `O(1)` y los elementos viven **contiguos** en el array denso, de modo que
/// iterarlos es cache-friendly.
///
/// Es la pieza típica de un **ECS**: almacenar los componentes de un tipo por entidad,
/// con altas y bajas frecuentes y recorrido rápido. La baja hace *swap-remove* (mueve el
/// último al hueco). Sin heap; `T` debe ser construible por defecto.
///
/// Uso:
///   eng::util::SparseSet<Velocity, 64> vels;
///   vels.insert(entity, {2, 0});
///   if (Velocity* v = vels.find(entity)) { ... }
///   for (Velocity& v : vels.values()) { ... }   // solo los presentes
///
/// Verificación: HOST-120.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>

namespace eng::util {

template <class T, eng::u16 MaxElements>
class SparseSet {
	static_assert(MaxElements > 0u, "SparseSet: MaxElements debe ser mayor que 0");

	static constexpr eng::u16 no_slot = 0xffffu;

public:
	/// Arranca vacío (todas las ranuras del índice disperso en `no_slot`).
	constexpr SparseSet() noexcept { clear(); }

	[[nodiscard]] static constexpr eng::u16 capacity() noexcept { return MaxElements; }
	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == MaxElements; }

	constexpr void clear() noexcept {
		for (eng::u16 i = 0u; i < MaxElements; ++i) {
			m_sparse[i] = no_slot;
		}
		m_size = 0u;
	}

	[[nodiscard]] constexpr bool contains(eng::u16 id) const noexcept {
		return id < MaxElements && m_sparse[id] != no_slot;
	}

	[[nodiscard]] constexpr T* find(eng::u16 id) noexcept {
		if (id >= MaxElements) {
			return nullptr;
		}
		const eng::u16 slot = m_sparse[id];
		return slot == no_slot ? nullptr : &m_values[slot];
	}
	[[nodiscard]] constexpr const T* find(eng::u16 id) const noexcept {
		if (id >= MaxElements) {
			return nullptr;
		}
		const eng::u16 slot = m_sparse[id];
		return slot == no_slot ? nullptr : &m_values[slot];
	}

	/// Inserta un id nuevo. `false` si ya existe o no cabe.
	constexpr bool insert(eng::u16 id, const T& value) noexcept {
		if (id >= MaxElements || m_sparse[id] != no_slot || m_size >= MaxElements) {
			return false;
		}
		const eng::u16 slot = m_size;
		m_dense_ids[slot] = id;
		m_values[slot] = value;
		m_sparse[id] = slot;
		++m_size;
		return true;
	}

	/// Asigna el valor; devuelve `true` si el id era nuevo. `false` si no cabe.
	constexpr bool insert_or_assign(eng::u16 id, const T& value) noexcept {
		if (id >= MaxElements) {
			return false;
		}
		const eng::u16 slot = m_sparse[id];
		if (slot != no_slot) {
			m_values[slot] = value;
			return false;
		}
		return insert(id, value);
	}

	/// Borra el id (swap-remove). `false` si no estaba.
	constexpr bool erase(eng::u16 id) noexcept {
		if (id >= MaxElements || m_size == 0u) {
			return false;
		}
		const eng::u16 slot = m_sparse[id];
		if (slot == no_slot) {
			return false;
		}
		const eng::u16 last = static_cast<eng::u16>(m_size - 1u);
		if (slot != last) {
			const eng::u16 moved = m_dense_ids[last];
			m_dense_ids[slot] = moved;
			m_values[slot] = m_values[last];
			m_sparse[moved] = slot;
		}
		m_sparse[id] = no_slot;
		--m_size;
		return true;
	}

	/// Ids presentes, en orden denso (para iterar junto a `values()`).
	[[nodiscard]] constexpr eng::Span<const eng::u16> ids() const noexcept {
		return eng::Span<const eng::u16> {m_dense_ids, m_size};
	}
	[[nodiscard]] constexpr eng::Span<const T> values() const noexcept {
		return eng::Span<const T> {m_values, m_size};
	}
	[[nodiscard]] constexpr eng::Span<T> values() noexcept {
		return eng::Span<T> {m_values, m_size};
	}

	[[nodiscard]] constexpr eng::u16 id_at(eng::usize slot) const noexcept {
		return m_dense_ids[slot];
	}
	[[nodiscard]] constexpr T& value_at(eng::usize slot) noexcept { return m_values[slot]; }
	[[nodiscard]] constexpr const T& value_at(eng::usize slot) const noexcept {
		return m_values[slot];
	}

private:
	eng::u16 m_sparse[MaxElements] {};  ///< id -> ranura densa (no_slot = ausente)
	eng::u16 m_dense_ids[MaxElements] {};
	T m_values[MaxElements] {};
	eng::u16 m_size = 0u;
};

} // namespace eng::util
