#pragma once

/// \file chunked_vector.hpp
/// `eng::util::ChunkedVector<T, Chunk, MaxChunks, A>`: secuencia que crece en
/// **bloques de tamaño fijo** asignados a un allocator, de modo que las direcciones
/// de los elementos **nunca se invalidan** (no hay realloc ni copia al crecer).
///
/// Es el contenedor para datos referenciados por punteros o handles estables
/// (streaming/append-only, caches de tiles, listas de objetos que el Blitter ya tiene
/// apuntados). A diferencia de `Vector`, crecer cuesta `O(1)` exacto (un bloque nuevo)
/// y no mueve lo ya insertado; el precio es que el acceso indexado paga una división
/// —por eso `Chunk` debe ser **potencia de dos** y se resuelve con un desplazamiento.
///
/// No libera memoria (los allocators del engine son bump); `clear()` reinicia el
/// tamaño pero **conserva** los bloques ya asignados para reutilizarlos.
///
/// Uso:
///   eng::util::ChunkedVector<eng::u16, 16, 8, eng::util::ArenaAlloc> rows {alloc};
///   rows.push_back(v);           // hasta 16·8 = 128 elementos; dir. estable

#include <eng/core/span.hpp>
#include <eng/core/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/type_traits.hpp>

namespace eng::util {

template <class T, usize Chunk, usize MaxChunks, class A = NullAlloc>
class ChunkedVector {
	static_assert(Chunk > 0u && MaxChunks > 0u, "ChunkedVector: Chunk y MaxChunks > 0");
	static_assert((Chunk & (Chunk - 1u)) == 0u, "ChunkedVector: Chunk debe ser potencia de dos");
	static_assert(is_trivially_copyable_v<T>, "ChunkedVector: T debe ser copiable trivialmente");

public:
	using value_type = T;

	constexpr ChunkedVector() noexcept = default;
	explicit constexpr ChunkedVector(A alloc) noexcept : m_alloc(alloc) {}

	ChunkedVector(const ChunkedVector&) = delete;
	ChunkedVector& operator=(const ChunkedVector&) = delete;

	constexpr ChunkedVector(ChunkedVector&& other) noexcept : m_alloc(other.m_alloc) {
		adopt(other);
	}
	constexpr ChunkedVector& operator=(ChunkedVector&& other) noexcept {
		if (this != &other) {
			m_alloc = other.m_alloc;
			adopt(other);
		}
		return *this;
	}

	[[nodiscard]] static constexpr usize chunk_size() noexcept { return Chunk; }
	[[nodiscard]] static constexpr usize chunk_count() noexcept { return MaxChunks; }
	[[nodiscard]] static constexpr usize capacity() noexcept { return Chunk * MaxChunks; }

	[[nodiscard]] constexpr usize size() const noexcept { return m_size; }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_size == 0u; }
	[[nodiscard]] constexpr bool full() const noexcept { return m_size == capacity(); }
	/// Bloques ya reservados (crece de uno en uno, nunca se liberan).
	[[nodiscard]] constexpr usize chunks_live() const noexcept { return m_chunks_live; }

	[[nodiscard]] constexpr T& operator[](usize index) noexcept {
		return m_chunks[index >> kLog2Chunk][index & (Chunk - 1u)];
	}
	[[nodiscard]] constexpr const T& operator[](usize index) const noexcept {
		return m_chunks[index >> kLog2Chunk][index & (Chunk - 1u)];
	}
	[[nodiscard]] constexpr T& at(usize index) noexcept {
		if (index >= m_size) {
			eng::detail::span_out_of_bounds();
		}
		return (*this)[index];
	}
	[[nodiscard]] constexpr const T& at(usize index) const noexcept {
		if (index >= m_size) {
			eng::detail::span_out_of_bounds();
		}
		return (*this)[index];
	}
	[[nodiscard]] constexpr T& front() noexcept { return at(0u); }
	[[nodiscard]] constexpr T& back() noexcept { return at(m_size - 1u); }

	/// Añade copiando. `false` si no cabe o si el allocator no puede dar un bloque.
	constexpr bool push_back(const T& value) noexcept {
		if (m_size == capacity()) {
			return false;
		}
		if (m_off == 0u && m_chunk >= m_chunks_live) {
			if (m_chunks_live >= MaxChunks) {
				return false;
			}
			const Span<u8> block = m_alloc.allocate(Chunk * sizeof(T), alignof(T));
			if (block.empty()) {
				return false;
			}
			m_chunks[m_chunks_live++] = reinterpret_cast<T*>(block.data());
		}
		m_chunks[m_chunk][m_off] = value;
		if (++m_off == Chunk) {
			m_off = 0u;
			++m_chunk;
		}
		++m_size;
		return true;
	}

	/// Vacía conservando los bloques ya reservados (se reutilizan al volver a llenar).
	constexpr void clear() noexcept {
		m_size = 0u;
		m_chunk = 0u;
		m_off = 0u;
	}

private:
	static constexpr usize kLog2Chunk = [] {
		usize e = 0u;
		usize s = Chunk;
		while (s > 1u) {
			s >>= 1u;
			++e;
		}
		return e;
	}();

	constexpr void adopt(ChunkedVector& other) noexcept {
		for (usize c = 0; c < MaxChunks; ++c) {
			m_chunks[c] = other.m_chunks[c];
		}
		m_chunks_live = other.m_chunks_live;
		m_size = other.m_size;
		m_chunk = other.m_chunk;
		m_off = other.m_off;
		other.m_chunks_live = 0u;
		other.m_size = 0u;
		other.m_chunk = 0u;
		other.m_off = 0u;
	}

	T* m_chunks[MaxChunks] {};
	usize m_chunks_live = 0u;
	usize m_size = 0u;
	usize m_chunk = 0u;
	usize m_off = 0u;
	A m_alloc {};
};

} // namespace eng::util
