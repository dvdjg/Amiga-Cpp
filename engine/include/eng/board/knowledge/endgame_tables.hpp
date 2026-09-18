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

// --- Serialización byte a byte (segura en 68000) ---

[[nodiscard]] inline u32 eg_read_u32(const u8* data) noexcept {
	return static_cast<u32>(data[0]) | (static_cast<u32>(data[1]) << 8u) |
	       (static_cast<u32>(data[2]) << 16u) | (static_cast<u32>(data[3]) << 24u);
}

[[nodiscard]] inline EndgameTableEntry read_endgame_entry(const u8* data) noexcept {
	EndgameTableEntry entry;
	entry.key = eg_read_u32(data);
	entry.value = static_cast<s16>(static_cast<u16>(data[4]) | (static_cast<u16>(data[5]) << 8u));
	entry.dtm = data[6];
	entry.reserved = data[7];
	return entry;
}

inline void write_endgame_entry(u8* data, const EndgameTableEntry& entry) noexcept {
	data[0] = static_cast<u8>(entry.key & 0xffu);
	data[1] = static_cast<u8>((entry.key >> 8u) & 0xffu);
	data[2] = static_cast<u8>((entry.key >> 16u) & 0xffu);
	data[3] = static_cast<u8>((entry.key >> 24u) & 0xffu);
	data[4] = static_cast<u8>(static_cast<u16>(entry.value) & 0xffu);
	data[5] = static_cast<u8>((static_cast<u16>(entry.value) >> 8u) & 0xffu);
	data[6] = entry.dtm;
	data[7] = entry.reserved;
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
