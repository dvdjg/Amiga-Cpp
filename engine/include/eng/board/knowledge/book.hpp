#pragma once

/// \file book.hpp
/// **Libro de aperturas** empaquetado: un array de entradas ordenadas por clave
/// Zobrist. En partida el motor hace búsqueda binaria (sin cargar el libro entero);
/// cada entrada devuelve la jugada, una puntuación y el identificador de nombre de
/// la apertura (que el explicador NLG convierte en texto).
///
/// Formato de una entrada (12 B, serializable a bloque sin problemas de alineación
/// en 68000):
///
///   key u32 | move u32 | score s16 | name_id u16
///
/// El libro se construye en el host (`tools/board/`) y viaja como bloque; el motor
/// lo lee con `read_book_entry` (ensamblado byte a byte, nunca `reinterpret_cast` a
/// un struct, que en 68000 fallaría por alineación).
///
/// Verificación: HOST-147.

#include <eng/board/core/types.hpp>
#include <eng/core/span.hpp>

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

// --- Serialización byte a byte (segura en 68000, sin alineación) ---

[[nodiscard]] inline u32 book_read_u32(const u8* data) noexcept {
	return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8u) |
	       (static_cast<u32>(data[2]) << 16u) | (static_cast<u32>(data[3]) << 24u);
}

[[nodiscard]] inline u16 book_read_u16(const u8* data) noexcept {
	return static_cast<u16>(static_cast<u16>(data[0]) | (static_cast<u16>(data[1]) << 8u));
}

[[nodiscard]] inline BookEntry read_book_entry(const u8* data) noexcept {
	BookEntry entry;
	entry.key = book_read_u32(data);
	entry.move = book_read_u32(data + 4);
	entry.score = static_cast<s16>(book_read_u16(data + 8));
	entry.name_id = book_read_u16(data + 10);
	return entry;
}

inline void book_write_u32(u8* data, u32 value) noexcept {
	data[0] = static_cast<u8>(value & 0xffu);
	data[1] = static_cast<u8>((value >> 8u) & 0xffu);
	data[2] = static_cast<u8>((value >> 16u) & 0xffu);
	data[3] = static_cast<u8>((value >> 24u) & 0xffu);
}

inline void book_write_u16(u8* data, u16 value) noexcept {
	data[0] = static_cast<u8>(value & 0xffu);
	data[1] = static_cast<u8>((value >> 8u) & 0xffu);
}

inline void write_book_entry(u8* data, const BookEntry& entry) noexcept {
	book_write_u32(data, entry.key);
	book_write_u32(data + 4, entry.move);
	book_write_u16(data + 8, static_cast<u16>(entry.score));
	book_write_u16(data + 10, entry.name_id);
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

/// Constructor del libro sobre un buffer del llamador (para el host o para
/// `init`). `add` no reserva; `finalize` ordena por clave.
class BookBuilder {
public:
	explicit BookBuilder(eng::Span<BookEntry> storage) noexcept : m_storage(storage) {}

	bool add(u32 key, Move move, s16 score = 0, u16 name_id = 0u) noexcept {
		if (m_count >= m_storage.size()) {
			return false;
		}
		m_storage[m_count] = BookEntry {key, move, score, name_id};
		++m_count;
		return true;
	}

	/// Ordena por clave (inserción; el libro se ordena una vez al construirlo).
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

/// Nombre de apertura `id` dentro de un pool de cadenas separadas por NUL.
[[nodiscard]] inline const char* book_name(eng::Span<const char> pool, u16 id) noexcept {
	if (pool.empty()) {
		return "";
	}
	const char* cursor = pool.data();
	const char* end = pool.data() + pool.size();
	for (u16 i = 0u; i < id && cursor < end; ++i) {
		while (cursor < end && *cursor != '\0') {
			++cursor;
		}
		if (cursor < end) {
			++cursor;
		}
	}
	return (cursor < end) ? cursor : "";
}

} // namespace eng::board
