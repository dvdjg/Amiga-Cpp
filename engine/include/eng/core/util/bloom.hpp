#pragma once

/// \file bloom.hpp
/// `eng::util::BloomFilter<T, Bits, HashCount>`: filtro de pertenencia probabilístico
/// de capacidad fija, sin heap. `may_contain(value) == false` garantiza que el valor no
/// se insertó; `true` solo significa que debe comprobarse en la estructura exacta.
///
/// El filtro no sustituye a `HashSet`/`HashMap`: permite omitir búsquedas exactas
/// negativas en conjuntos grandes y estables durante una operación acotada. Es
/// insert-only; `clear()` inicia una nueva operación. La capacidad `Bits` es potencia de
/// dos y `HashCount` usa doble hashing derivado del hash del engine.
///
/// Memoria: `Bits / 8` bytes de datos más cuatro contadores `u32` para medir consultas,
/// negativos, posibles coincidencias y falsos positivos exactos. El almacenamiento total
/// es `sizeof(BloomFilter<T, Bits, HashCount, Hasher>)`; no hay memoria dinámica.
/// El hash usa `eng::util::Hash<T>` (sin multiplicación 32×32 en las claves del engine).
///
/// **Nota de uso**: un filtro Bloom compensa cuando la comprobación exacta es **cara** (E/S,
/// disco, red); sobre una comprobación **in-memory O(1)** (p. ej. el `m_best` de GOAP) **no
/// mejora**: `may_contain` recalcula el hash que la búsqueda exacta volvería a hacer y su trabajo
/// (hash del `step` + pruebas de bit) supera la sonda evitada. Ver
/// docs/engine/architecture/GAME_AI_LIBRARY.md §3.1 (HOST-426).
///
/// Uso:
///   eng::util::BloomFilter<u32, 1024> maybe_seen;
///   if (maybe_seen.may_contain(key)) exact_set.contains(key);
///   else maybe_seen.insert(key); // negativo: se sabe que no estaba
///
/// Verificación: HOST-426.

#include <eng/core/types/types.hpp>
#include <eng/core/util/hash.hpp>

namespace eng::util {

/// Conteos de diagnóstico del filtro; los contadores envuelven módulo 2^32.
struct BloomFilterStats {
	u32 queries = 0u; ///< llamadas a `may_contain`
	u32 negatives = 0u; ///< respuestas garantizadas como ausentes
	u32 possibles = 0u; ///< respuestas que requieren consultar la estructura exacta
	u32 false_positives = 0u; ///< respuestas positivas que la estructura exacta rechazó
};

/// Filtro Bloom insert-only. Se especializa para clave, número de bits y hashes.
template <class T, usize Bits, usize HashCount = 4u, class Hasher = Hash<T>>
class BloomFilter {
	static_assert(Bits >= 32u && (Bits & (Bits - 1u)) == 0u,
	              "BloomFilter: Bits debe ser potencia de dos y >= 32");
	static_assert(Bits <= 0x80000000u, "BloomFilter: Bits debe caber en el espacio de hash u32");
	static_assert(HashCount > 0u && HashCount <= 8u, "BloomFilter: HashCount debe estar en [1,8]");

public:
	static constexpr usize bit_capacity = Bits;
	static constexpr usize hash_count = HashCount;
	static constexpr usize storage_bytes = Bits / 8u;

	/// Borra bits y diagnósticos; se usa al empezar una nueva búsqueda/conjunto.
	constexpr void clear() noexcept {
		for (usize i = 0u; i < word_count; ++i) m_words[i] = 0u;
		m_stats = {};
	}

	/// Inserta `value`; los duplicados no cambian el conjunto de bits.
	constexpr void insert(const T& value) noexcept {
		u32 bit_hash = m_hash(value);
		const u32 step = hash_u32(bit_hash ^ 0x9e3779b9u) | 1u;
		for (usize i = 0u; i < HashCount; ++i, bit_hash += step) {
			// `bit_hash` (u32) se promueve a `usize` al operar con `Bits` (usize): sin cast.
			const usize bit = bit_hash & (Bits - 1u);
			m_words[bit / word_bits] |= bit_mask(bit);
		}
	}

	/// `false` garantiza ausencia; `true` requiere confirmar con la estructura exacta.
	[[nodiscard]] constexpr bool may_contain(const T& value) noexcept {
		++m_stats.queries;
		u32 bit_hash = m_hash(value);
		const u32 step = hash_u32(bit_hash ^ 0x9e3779b9u) | 1u;
		for (usize i = 0u; i < HashCount; ++i, bit_hash += step) {
			const usize bit = bit_hash & (Bits - 1u);
			if ((m_words[bit / word_bits] & bit_mask(bit)) == 0u) {
				++m_stats.negatives;
				return false;
			}
		}
		++m_stats.possibles;
		return true;
	}

	/// Registra que la estructura exacta rechazó una posible coincidencia.
	constexpr void note_false_positive() noexcept { ++m_stats.false_positives; }

	/// Devuelve una copia de los contadores de la operación actual.
	[[nodiscard]] constexpr BloomFilterStats stats() const noexcept { return m_stats; }

private:
	using Word = __UINT32_TYPE__;
	static constexpr usize word_bits = 32u;
	static constexpr usize word_count = Bits / word_bits;

	/// Máscara del bit global `bit` dentro de su palabra de 32 bits.
	[[nodiscard]] static constexpr Word bit_mask(usize bit) noexcept {
		// `one` es `Word` (32 bits garantizados): el desplazamiento no depende de `unsigned int`.
		constexpr Word one = 1u;
		return one << (bit % word_bits);
	}

	Word m_words[word_count] {}; ///< representación compacta del conjunto probabilístico
	Hasher m_hash {}; ///< estrategia de hash del tipo de clave
	BloomFilterStats m_stats {}; ///< diagnósticos de consulta, no intervienen en el resultado
};

} // namespace eng::util
