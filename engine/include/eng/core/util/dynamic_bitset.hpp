#pragma once

/// \file dynamic_bitset.hpp
/// `eng::util::DynamicBitSet<A>`: conjunto de bits de tamaño **elegido en `init`** que
/// reserva sus palabras en un `Allocator` (arena/bump), sin heap. Complementa a
/// `BitSet<N>` (tamaño fijo en compilación): úsalo cuando el número de bits solo se
/// conoce en carga (tiles sucios de un chunk grande, flags de entidades, máscaras).
///
/// El crecimiento se hace **solo en `init`**; en `frame` no se reserva. Las palabras son
/// de 32 bits exactos (`__UINT32_TYPE__`), como `BitSet`, para el mismo ancho en m68k y
/// en host.
///
/// Uso:
///   eng::util::InlineAlloc<256> alloc;
///   eng::util::DynamicBitSet<eng::util::InlineAlloc<256>> bits {alloc};
///   bits.init(1000);              // 32 palabras (128 B)
///   bits.set(999);
///   const eng::usize n = bits.count();
///
/// Verificación: HOST-122.

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/allocator.hpp>
#include <eng/core/util/bit.hpp>

namespace eng::util {

template <class A>
class DynamicBitSet {
	static_assert(Allocator<A>, "DynamicBitSet: A debe cumplir Allocator");

public:
	using word_type = __UINT32_TYPE__;
	static constexpr eng::usize word_bits = 32u;

	constexpr explicit DynamicBitSet(A& alloc) noexcept : m_alloc(&alloc) {}

	/// Reserva memoria para `bits` bits (solo en `init`). `false` si el asignador no
	/// entrega. Deja todos los bits a 0.
	[[nodiscard]] constexpr bool init(eng::usize bits) noexcept {
		const eng::usize words = (bits + word_bits - 1u) / word_bits;
		if (words == 0u) {
			m_words = eng::Span<word_type> {};
			m_bits = 0u;
			return true;
		}
		const eng::Span<eng::u8> raw =
			m_alloc->allocate(words * sizeof(word_type), alignof(word_type));
		if (raw.data() == nullptr || raw.size() < words * sizeof(word_type)) {
			return false;
		}
		m_words = eng::Span<word_type> {reinterpret_cast<word_type*>(raw.data()), words};
		m_bits = bits;
		clear();
		return true;
	}

	[[nodiscard]] constexpr eng::usize size() const noexcept { return m_bits; }
	[[nodiscard]] constexpr eng::usize word_count() const noexcept { return m_words.size(); }
	[[nodiscard]] constexpr bool empty() const noexcept { return m_bits == 0u; }

	[[nodiscard]] constexpr word_type* words() noexcept { return m_words.data(); }
	[[nodiscard]] constexpr const word_type* words() const noexcept { return m_words.data(); }

	constexpr void clear() noexcept {
		for (eng::usize w = 0; w < m_words.size(); ++w) {
			m_words[w] = 0u;
		}
	}

	constexpr void set(eng::usize i) noexcept {
		check(i);
		m_words[i / word_bits] =
			static_cast<word_type>(m_words[i / word_bits] | bit_of(i));
	}
	constexpr void set(eng::usize i, bool value) noexcept {
		if (value) {
			set(i);
		} else {
			reset(i);
		}
	}
	constexpr void reset(eng::usize i) noexcept {
		check(i);
		m_words[i / word_bits] =
			static_cast<word_type>(m_words[i / word_bits] & static_cast<word_type>(~bit_of(i)));
	}
	constexpr void flip(eng::usize i) noexcept {
		check(i);
		m_words[i / word_bits] =
			static_cast<word_type>(m_words[i / word_bits] ^ bit_of(i));
	}
	[[nodiscard]] constexpr bool test(eng::usize i) const noexcept {
		check(i);
		return (m_words[i / word_bits] & bit_of(i)) != 0u;
	}

	[[nodiscard]] constexpr eng::usize count() const noexcept {
		const eng::usize full = m_bits / word_bits;
		const eng::usize rem = m_bits % word_bits;
		eng::usize n = 0u;
		for (eng::usize w = 0; w < full; ++w) {
			n += static_cast<eng::usize>(popcount(m_words[w]));
		}
		if (rem != 0u) {
			n += static_cast<eng::usize>(
				popcount(static_cast<word_type>(m_words[full] & mask_of(rem))));
		}
		return n;
	}

	[[nodiscard]] constexpr bool any() const noexcept {
		const eng::usize full = m_bits / word_bits;
		const eng::usize rem = m_bits % word_bits;
		for (eng::usize w = 0; w < full; ++w) {
			if (m_words[w] != 0u) {
				return true;
			}
		}
		if (rem != 0u) {
			return (m_words[full] & mask_of(rem)) != 0u;
		}
		return false;
	}
	[[nodiscard]] constexpr bool none() const noexcept { return !any(); }

private:
	[[nodiscard]] static constexpr word_type mask_of(eng::usize k) noexcept {
		return static_cast<word_type>((static_cast<word_type>(1) << k) - 1u);
	}
	[[nodiscard]] static constexpr word_type bit_of(eng::usize i) noexcept {
		return static_cast<word_type>(static_cast<word_type>(1) << (i % word_bits));
	}
	constexpr void check(eng::usize i) const noexcept {
		if (i >= m_bits) {
			eng::detail::span_out_of_bounds();
		}
	}

	A* m_alloc;
	eng::Span<word_type> m_words {};
	eng::usize m_bits = 0u;
};

} // namespace eng::util
