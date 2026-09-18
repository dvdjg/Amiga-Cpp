#pragma once

/// \file tt.hpp
/// **Tabla de transposición** del motor de tablero: guarda posiciones ya
/// analizadas (clave Zobrist → mejor jugada, puntuación, profundidad y tipo de
/// cota) para no repetir trabajo y para que el *pondering* reutilice el árbol.
///
/// La entrada se empaqueta en **12 bytes** (decisión de diseño del motor):
///
///   ┌────────────┬──────────┬───────────────┬─────────────┬──────────────┐
///   │ key u32    │ score s16│ depth+flag u8 │ reservado u8│ move u32     │
///   └────────────┴──────────┴───────────────┴─────────────┴──────────────┘
///
/// No se usa `unsigned long long` para la clave: en 68000 sería un libcall. La
/// tabla se dimensiona en `init` según el perfil de memoria (`core/budget.hpp`).
///
/// Verificación: HOST-144.

#include <eng/board/core/types.hpp>

namespace eng::board {

/// Tipo de cota guardada (parte baja de `depth_flags`).
enum class TtFlag : u8 {
	None = 0u,
	Exact = 1u, ///< puntuación exacta
	Alpha = 2u, ///< cota superior (falló alto)
	Beta = 3u,  ///< cota inferior (falló bajo)
};

/// Entrada empaquetada. `depth_flags` = (profundidad << 2) | flag.
struct TtEntry {
	u32 key = 0u;
	s16 score = 0u;
	u8 depth_flags = 0u;
	u8 reserved = 0u;
	Move move = kNoMove;
};

static_assert(sizeof(TtEntry) == 12u, "TtEntry debe empaquetarse en 12 bytes");

/// Tabla de transposición de capacidad fija (potencia de dos). Índice por máscara
/// (sin módulo, que en 68000 es caro). Reemplazo simple: la entrada nueva pisa la
/// vieja.
template <u32 Entries>
class TranspositionTable {
	static_assert(Entries > 0u, "TranspositionTable: Entries > 0");
	static_assert((Entries & (Entries - 1u)) == 0u, "TranspositionTable: Entries potencia de 2");

public:
	static constexpr u32 entry_count = Entries;
	static constexpr u32 entry_bytes = sizeof(TtEntry);
	static constexpr u32 mask = Entries - 1u;

	void clear() noexcept {
		for (u32 i = 0u; i < Entries; ++i) {
			m_entries[i].key = 0u;
			m_entries[i].score = 0;
			m_entries[i].depth_flags = 0u;
			m_entries[i].move = kNoMove;
		}
	}

	/// Consulta. Si devuelve `true`, `out_*` son válidos (aunque la profundidad
	/// guardada puede ser menor que la pedida: lo decide el buscador).
	[[nodiscard]] bool probe(u32 key, Move& out_move, Score& out_score, u32& out_depth,
	                         TtFlag& out_flag) const noexcept {
		const TtEntry& entry = m_entries[key & mask];
		if (entry.key != key || entry.depth_flags == 0u) {
			return false;
		}
		out_move = entry.move;
		out_score = entry.score;
		out_depth = static_cast<u32>(entry.depth_flags >> 2u);
		out_flag = static_cast<TtFlag>(entry.depth_flags & 0x03u);
		return true;
	}

	/// Escribe (reemplazo directo). La profundidad se satura a 63.
	void store(u32 key, u32 depth, TtFlag flag, Score score, Move move) noexcept {
		TtEntry& entry = m_entries[key & mask];
		entry.key = key;
		entry.score = score;
		const u32 clamped = (depth > 63u) ? 63u : depth;
		entry.depth_flags = static_cast<u8>((clamped << 2u) | static_cast<u8>(flag));
		entry.reserved = 0u;
		entry.move = move;
	}

private:
	TtEntry m_entries[Entries] {};
};

} // namespace eng::board
