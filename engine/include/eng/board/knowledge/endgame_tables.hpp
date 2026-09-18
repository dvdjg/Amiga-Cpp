#pragma once

/// \file endgame_tables.hpp
/// **Tablas de finales teóricos** (nivel 3): resultados y distancia a la conversión
/// para finales de 3–4 piezas. Son datos voluminosos que viven en disquete; en RAM
/// quedan el índice y la caché de bloques (`knowledge/cache.hpp`).
///
/// Formato de entrada (8 B, serializable a bloque):
///
///   key u32 | value s16 | dtm u8 | reservado u8
///
/// `key` es la clave Zobrist de la posición (incluye turno), `value` la puntuación
/// (mate o 0 para tablas) y `dtm` la distancia a la conversión. El motor sondea por
/// clave exacta con búsqueda binaria. Las tablas las genera el host offline
/// (`tools/board/`); aquí solo está el contenedor y la consulta.
///
/// Verificación: HOST-147.

#include <eng/board/core/types.hpp>
#include <eng/core/span.hpp>
#include <eng/core/util/binary.hpp>

namespace eng::board {

/// Entrada de la tabla de finales.
struct EndgameTableEntry {
	u32 key = 0u;
	s16 value = 0;
	u8 dtm = 0u;
	u8 reserved = 0u;
};

static_assert(sizeof(EndgameTableEntry) == 8u, "EndgameTableEntry debe medir 8 bytes");

/// Resultado de consultar la tabla.
struct EndgameTableProbe {
	s16 value = 0;
	u8 dtm = 0u;
	bool found = false;
};

// --- Serialización con cursores seguros (little-endian, sin alineación) ---

/// Lee una entrada (8 B) del cursor. `false` si no caben.
[[nodiscard]] inline bool read_endgame_entry(eng::util::ByteReader& reader,
                                             EndgameTableEntry& out) noexcept {
	return reader.read_u32(out.key) && reader.read_s16(out.value) && reader.read_u8(out.dtm) &&
	       reader.read_u8(out.reserved);
}

/// Escribe una entrada (8 B) en el cursor. `false` si no caben.
[[nodiscard]] inline bool write_endgame_entry(eng::util::ByteWriter& writer,
                                              const EndgameTableEntry& entry) noexcept {
	return writer.write_u32(entry.key) && writer.write_s16(entry.value) &&
	       writer.write_u8(entry.dtm) && writer.write_u8(entry.reserved);
}

/// Búsqueda binaria por clave sobre entradas **ordenadas**.
[[nodiscard]] inline EndgameTableProbe probe_endgame_table(
    eng::Span<const EndgameTableEntry> entries, u32 key) noexcept {
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
	EndgameTableProbe probe;
	if (lo < entries.size() && entries[lo].key == key) {
		probe.value = entries[lo].value;
		probe.dtm = entries[lo].dtm;
		probe.found = true;
	}
	return probe;
}

/// Constructor de una tabla sobre un buffer del llamador.
class EndgameTableBuilder {
public:
	explicit EndgameTableBuilder(eng::Span<EndgameTableEntry> storage) noexcept
	    : m_storage(storage) {}

	bool add(u32 key, s16 value, u8 dtm = 0u) noexcept {
		if (m_count >= m_storage.size()) {
			return false;
		}
		m_storage[m_count] = EndgameTableEntry {key, value, dtm, 0u};
		++m_count;
		return true;
	}

	void finalize() noexcept {
		for (u32 i = 1u; i < m_count; ++i) {
			const EndgameTableEntry value = m_storage[i];
			u32 j = i;
			while (j > 0u && m_storage[j - 1u].key > value.key) {
				m_storage[j] = m_storage[j - 1u];
				--j;
			}
			m_storage[j] = value;
		}
	}

	[[nodiscard]] u32 size() const noexcept { return m_count; }
	[[nodiscard]] eng::Span<const EndgameTableEntry> entries() const noexcept {
		return eng::Span<const EndgameTableEntry> {m_storage.data(), m_count};
	}

private:
	eng::Span<EndgameTableEntry> m_storage {};
	u32 m_count = 0u;
};

} // namespace eng::board
