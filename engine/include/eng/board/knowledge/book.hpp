#pragma once

/// \file book.hpp
/// **Libro de aperturas** empaquetado: entradas ordenadas por clave Zobrist. En
/// partida el motor hace búsqueda binaria (sin cargar el libro entero); cada entrada
/// devuelve la jugada, una puntuación y el identificador de nombre de la apertura
/// (que el explicador NLG convierte en texto).
///
/// Formato de una entrada (12 B): `key u32 | move u32 | score s16 | name_id u16`.
///
/// El parseo/serializado usa `eng::util::ByteReader`/`ByteWriter` (little-endian y
/// **sin `reinterpret_cast`**), de modo que no hay aritmética de punteros ni fallos
/// de alineación en 68000; el libro se lee de un bloque con `read_book_entry`.
///
/// Verificación: HOST-147.

#include <eng/board/core/types.hpp>
#include <eng/core/span.hpp>
#include <eng/core/util/binary.hpp>
#include <eng/core/util/string_view.hpp>

namespace eng::board {

/// Entrada del libro de aperturas.
struct BookEntry {
	u32 key = 0u;
	Move move = kNoMove;
	s16 score = 0;
	u16 name_id = 0u;
};

static_assert(sizeof(BookEntry) == 12u, "BookEntry debe medir 12 bytes");

/// Resultado de consultar el libro.
struct BookProbe {
	Move move = kNoMove;
	s16 score = 0;
	u16 name_id = 0u;
	bool found = false;
};

/// Lee una entrada del libro del cursor. `false` si no hay 12 bytes.
[[nodiscard]] inline bool read_book_entry(eng::util::ByteReader& reader, BookEntry& out) noexcept {
	return reader.read_u32(out.key) && reader.read_u32(out.move) && reader.read_s16(out.score) &&
	       reader.read_u16(out.name_id);
}

/// Escribe una entrada del libro en el cursor. `false` si no caben 12 bytes.
[[nodiscard]] inline bool write_book_entry(eng::util::ByteWriter& writer,
                                           const BookEntry& entry) noexcept {
	return writer.write_u32(entry.key) && writer.write_u32(entry.move) &&
	       writer.write_s16(entry.score) && writer.write_u16(entry.name_id);
}

/// Búsqueda binaria por clave sobre entradas **ordenadas**.
[[nodiscard]] inline BookProbe probe_book(eng::Span<const BookEntry> entries,
                                          u32 key) noexcept {
	eng::usize lo = 0u;
	eng::usize hi = entries.size();
	while (lo < hi) {
		const eng::usize mid = lo + (hi - lo) / 2u;
		if (entries[mid].key < key) {
			lo = mid + 1u;
		} else {
			hi = mid;
		}
	}
	BookProbe probe;
	if (lo < entries.size() && entries[lo].key == key) {
		probe.move = entries[lo].move;
		probe.score = entries[lo].score;
		probe.name_id = entries[lo].name_id;
		probe.found = true;
	}
	return probe;
}

/// Constructor del libro sobre un buffer del llamador. `add` no reserva; `finalize`
/// ordena por clave.
class BookBuilder {
public:
	constexpr explicit BookBuilder(eng::Span<BookEntry> storage) noexcept : m_storage(storage) {}

	[[nodiscard]] bool add(u32 key, Move move, s16 score = 0, u16 name_id = 0u) noexcept {
		if (m_count >= m_storage.size()) {
			return false;
		}
		m_storage[m_count] = BookEntry {key, move, score, name_id};
		++m_count;
		return true;
	}

	void finalize() noexcept {
		for (u32 i = 1u; i < m_count; ++i) {
			const BookEntry value = m_storage[i];
			u32 j = i;
			while (j > 0u && m_storage[j - 1u].key > value.key) {
				m_storage[j] = m_storage[j - 1u];
				--j;
			}
			m_storage[j] = value;
		}
	}

	[[nodiscard]] u32 size() const noexcept { return m_count; }
	[[nodiscard]] eng::Span<const BookEntry> entries() const noexcept {
		return eng::Span<const BookEntry> {m_storage.data(), m_count};
	}

private:
	eng::Span<BookEntry> m_storage {};
	u32 m_count = 0u;
};

/// Índice del primer NUL a partir de `from` (o el tamaño si no hay).
[[nodiscard]] inline eng::usize find_nul(eng::Span<const char> pool, eng::usize from) noexcept {
	eng::usize i = from;
	while (i < pool.size() && pool[i] != '\0') {
		++i;
	}
	return i;
}

/// Nombre de apertura `id` dentro de un pool de cadenas separadas por NUL. Devuelve
/// `StringView` (vista, sin copia) o vacío si el id se sale del pool.
[[nodiscard]] inline eng::util::StringView book_name(eng::Span<const char> pool, u16 id) noexcept {
	eng::usize begin = 0u;
	for (u16 i = 0u; i < id; ++i) {
		const eng::usize next = find_nul(pool, begin);
		if (next >= pool.size()) {
			return {};
		}
		begin = next + 1u;
	}
	const eng::usize end = find_nul(pool, begin);
	return eng::util::StringView {pool.subspan(begin, end - begin)};
}

} // namespace eng::board
