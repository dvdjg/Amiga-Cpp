#pragma once

/// \file bitset.hpp
/// `eng::util::BitSet<N>`: conjunto de bits de tamaño fijo (`std::bitset`).
///
/// Sirve para los muchos usos de "conjunto de flags" del engine sin reservar
/// memoria: tiles sucios de un chunk, canales de sprite libres, bits de
/// colisión de un objeto, máscaras de planos o `saveword`. El tamaño es una
/// constante de compilación, así que el almacenamiento va inline en el objeto y
/// no hay punteros ni asignación.
///
/// Representación: palabras de 32 bits exactos (`__UINT32_TYPE__`), no `unsigned
/// long`, para que el ancho sea el mismo en m68k y en el host de tests. Los bits
/// por encima de `N` en la última palabra no cuentan en `count`/`all`/`any`.
///
/// Uso:
///   eng::util::BitSet<8> used;      // 8 canales de sprite
///   used.set(3);
///   if (!used.test(3)) { ... }      // nunca imprime: se marcó
///   used.set(3, false);             // borrar
///
/// `set(i)`/`test(i)`/`flip(i)` comprueban el rango y disparan `illegal` (0x4afc)
/// en m68k ante un índice inválido (mismo contrato que `Span::at`).
///
/// Verificación: HOST-076 y demo `086_bob_objects` (`ActorStore`, `build -> run ->
/// analyze` OK).

#include <eng/core/types/span.hpp>
#include <eng/core/types/types.hpp>
#include <eng/core/util/bit.hpp>

namespace eng::util {

template <usize N>
class BitSet {
	static_assert(N > 0u, "BitSet: N debe ser mayor que 0");

public:
	using word_type = __UINT32_TYPE__;

	static constexpr usize bits = N;
	static constexpr usize word_bits = 32u;
	static constexpr usize word_count = (N + word_bits - 1u) / word_bits;

	constexpr BitSet() noexcept = default;

	/// Marca el bit `i`.
	constexpr void set(usize i) noexcept {
		check(i);
		m_words[i / word_bits] = static_cast<word_type>(m_words[i / word_bits] | bit_of(i));
	}

	/// Fija el bit `i` a `value`.
	constexpr void set(usize i, bool value) noexcept {
		if (value) {
			set(i);
		} else {
			reset(i);
		}
	}

	/// Borra el bit `i`.
	constexpr void reset(usize i) noexcept {
		check(i);
		m_words[i / word_bits] =
			static_cast<word_type>(m_words[i / word_bits] & static_cast<word_type>(~bit_of(i)));
	}

	/// Borra todos los bits.
	constexpr void reset() noexcept {
		for (usize w = 0; w < word_count; ++w) {
			m_words[w] = 0u;
		}
	}

	/// Invierte el bit `i`.
	constexpr void flip(usize i) noexcept {
		check(i);
		m_words[i / word_bits] = static_cast<word_type>(m_words[i / word_bits] ^ bit_of(i));
	}

	/// Invierte todos los bits (los que quedan por encima de `N` siguen a 0).
	constexpr void flip() noexcept {
		for (usize w = 0; w < word_count; ++w) {
			m_words[w] = static_cast<word_type>(~m_words[w]);
		}
		m_words[word_count - 1u] = static_cast<word_type>(m_words[word_count - 1u] & trailing_mask());
	}

	/// ¿Está el bit `i` a 1?
	[[nodiscard]] constexpr bool test(usize i) const noexcept {
		check(i);
		return (m_words[i / word_bits] & bit_of(i)) != 0u;
	}

	/// Cuántos bits están a 1.
	[[nodiscard]] constexpr usize count() const noexcept {
		usize n = 0;
		constexpr usize full = N / word_bits;
		constexpr usize rem = N % word_bits;
		for (usize w = 0; w < full; ++w) {
			n += static_cast<usize>(popcount(m_words[w]));
		}
		if (rem != 0u) {
			n += static_cast<usize>(popcount(static_cast<word_type>(m_words[full] & mask_of(rem))));
		}
		return n;
	}

	/// ¿Algún bit a 1 dentro de `N`?
	[[nodiscard]] constexpr bool any() const noexcept {
		constexpr usize full = N / word_bits;
		constexpr usize rem = N % word_bits;
		for (usize w = 0; w < full; ++w) {
			if (m_words[w] != 0u) {
				return true;
			}
		}
		if (rem != 0u) {
			return (m_words[full] & mask_of(rem)) != 0u;
		}
		return false;
	}

	/// ¿Ningún bit a 1 dentro de `N`?
	[[nodiscard]] constexpr bool none() const noexcept { return !any(); }

	/// ¿Todos los bits a 1?
	[[nodiscard]] constexpr bool all() const noexcept { return count() == N; }

	/// Alias de `reset()` para el vocabulario de buffers (`clear_bytes`).
	constexpr void clear() noexcept { reset(); }

	[[nodiscard]] constexpr word_type* words() noexcept { return m_words; }
	[[nodiscard]] constexpr const word_type* words() const noexcept { return m_words; }

	[[nodiscard]] friend constexpr bool operator==(const BitSet& a, const BitSet& b) noexcept {
		for (usize w = 0; w < word_count; ++w) {
			if (a.m_words[w] != b.m_words[w]) {
				return false;
			}
		}
		return true;
	}

private:
	/// Máscara con los `k` bits bajos a 1 (`k` en `[0, 32)`).
	[[nodiscard]] static constexpr word_type mask_of(usize k) noexcept {
		return static_cast<word_type>((static_cast<word_type>(1) << k) - 1u);
	}

	/// Máscara de los bits válidos de la última palabra.
	[[nodiscard]] static constexpr word_type trailing_mask() noexcept {
		constexpr usize rem = N % word_bits;
		return rem == 0u ? static_cast<word_type>(~static_cast<word_type>(0)) : mask_of(rem);
	}

	[[nodiscard]] static constexpr word_type bit_of(usize i) noexcept {
		return static_cast<word_type>(static_cast<word_type>(1) << (i % word_bits));
	}

	static constexpr void check(usize i) noexcept {
		if (i >= N) {
			eng::detail::span_out_of_bounds();
		}
	}

	word_type m_words[word_count] {};
};

} // namespace eng::util
