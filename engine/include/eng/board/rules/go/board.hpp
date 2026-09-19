#pragma once

/// \file board.hpp
/// Estado de una partida de **Go 9×9** sobre un array de 81 bytes, con cálculo de
/// grupos/libertades por *flood fill* y captura de grupos sin libertades.
///
/// Decisiones (68000):
/// - Tablero de 81 celdas (un byte por punto); `idx = rank*9 + file`. Grupos y
///   libertades se calculan con una búsqueda en anchura sobre 4 vecinos, sin
///   union-find (el tablero es diminuto y el coste es acotado y visible).
/// - La jugada de Go usa el `Move` genérico del engine codificando el **punto** en
///   los 8 bits bajos y un bit de **captura** (`kGoCaptureFlag`); así `is_capture`
///   es O(1) para la ordenación y la quiescence, sin consultar el tablero.
/// - **Ko simple**: tras una captura de una sola piedra cuyo grupo capturador queda
///   con una única libertad (la casilla capturada), se prohíbe recapturar ahí de
///   inmediato. El ko de superposición (superko) queda pendiente.
///
/// Verificación: HOST-179.

#include <eng/board/core/types.hpp>
#include <eng/board/core/zobrist.hpp>

namespace eng::board::go {

inline constexpr u8 kSize = 9u;
inline constexpr u8 kCells = static_cast<u8>(kSize * kSize); // 81
inline constexpr u8 kEmpty = 0u;
inline constexpr u8 kBlack = 1u;
inline constexpr u8 kWhite = 2u;
inline constexpr u8 kNoPoint = 0xffu;
inline constexpr Move kGoCaptureFlag = 0x100u;
inline constexpr Move kGoPass = 0xffu; ///< pasar turno

/// Número de claves Zobrist recordadas para el **superko posicional** (aproximado;
/// cubre las repeticiones recientes, que son las que aparecen en partida).
inline constexpr u8 kGoHistory = 32;

/// Jugada de Go: punto (8 bits) + flag de captura.
[[nodiscard]] constexpr Move go_move(u8 point, bool captures = false) noexcept {
	return static_cast<Move>(point) | (captures ? kGoCaptureFlag : 0u);
}
[[nodiscard]] constexpr u8 go_point(Move move) noexcept {
	return static_cast<u8>(move & 0xffu);
}
[[nodiscard]] constexpr bool go_is_pass(Move move) noexcept { return move == kGoPass; }

[[nodiscard]] constexpr bool point_valid(u8 point) noexcept { return point < kCells; }
[[nodiscard]] constexpr u8 point_file(u8 point) noexcept { return static_cast<u8>(point % kSize); }
[[nodiscard]] constexpr u8 point_rank(u8 point) noexcept { return static_cast<u8>(point / kSize); }
[[nodiscard]] constexpr u8 make_point(u8 file, u8 rank) noexcept {
	return static_cast<u8>(rank * kSize + file);
}

[[nodiscard]] constexpr u8 opposite_stone(u8 color) noexcept {
	return (color == kBlack) ? kWhite : kBlack;
}

/// Estado de la partida.
struct Position {
	u8 board[kCells] {}; ///< 0 vacío, 1 negro, 2 blanco
	u8 to_move = kBlack;
	u8 ko_point = kNoPoint;
	u8 passes = 0u; ///< pases consecutivos (>= 2 termina la partida)
	u8 captures[3] {}; ///< piedras capturadas por [color]
	u32 key = 0u;
	u32 history[kGoHistory] {}; ///< claves previas (superko aproximado)
	u8 history_count = 0u;
};

/// Zobrist de Go: celda×color + turno.
struct GoZobrist {
	u32 cell[kCells][2];
	u32 side;
};

[[nodiscard]] constexpr GoZobrist make_go_zobrist() noexcept {
	GoZobrist table {};
	u32 state = 0x9e3779b9u;
	for (u8 i = 0u; i < kCells; ++i) {
		table.cell[i][0] = zobrist_step(state);
		table.cell[i][1] = zobrist_step(state);
	}
	table.side = zobrist_step(state);
	return table;
}

inline constexpr GoZobrist kGoZobrist = make_go_zobrist();

[[nodiscard]] inline u32 compute_key(const Position& pos) noexcept {
	u32 key = 0u;
	for (u8 i = 0u; i < kCells; ++i) {
		const u8 stone = pos.board[i];
		if (stone == kBlack) {
			key ^= kGoZobrist.cell[i][0];
		} else if (stone == kWhite) {
			key ^= kGoZobrist.cell[i][1];
		}
	}
	if (pos.to_move == kWhite) {
		key ^= kGoZobrist.side;
	}
	return key;
}

/// Empuja `key` al historial circular (para el superko).
inline void push_history(Position& pos, u32 key) noexcept {
	if (pos.history_count < kGoHistory) {
		pos.history[pos.history_count] = key;
		++pos.history_count;
	} else {
		for (u8 i = 1u; i < kGoHistory; ++i) {
			pos.history[i - 1u] = pos.history[i];
		}
		pos.history[kGoHistory - 1u] = key;
	}
}

/// ¿Coincide `key` con la posición actual o con alguna del historial? (superko).
[[nodiscard]] inline bool position_repeated(const Position& pos, u32 key) noexcept {
	if (pos.key == key) {
		return true;
	}
	for (u8 i = 0u; i < pos.history_count; ++i) {
		if (pos.history[i] == key) {
			return true;
		}
	}
	return false;
}

/// Vecinos ortogonales de un punto (0..4), escritos en `out`.
[[nodiscard]] inline u8 neighbors(u8 point, u8 out[4]) noexcept {
	u8 count = 0u;
	const u8 file = point_file(point);
	const u8 rank = point_rank(point);
	if (file > 0u) out[count++] = static_cast<u8>(point - 1u);
	if (file + 1u < kSize) out[count++] = static_cast<u8>(point + 1u);
	if (rank > 0u) out[count++] = static_cast<u8>(point - kSize);
	if (rank + 1u < kSize) out[count++] = static_cast<u8>(point + kSize);
	return count;
}

/// Cuenta las libertades del grupo que contiene `start` (no toca `visited` salvo
/// para no repetir; `visited` es scratch de 81 bytes del llamador).
[[nodiscard]] inline u8 group_liberties(const Position& pos, u8 start, u8* visited) noexcept {
	for (u8 i = 0u; i < kCells; ++i) visited[i] = 0u;
	const u8 color = pos.board[start];
	u8 stack[kCells];
	u8 top = 0u;
	stack[top++] = start;
	visited[start] = 1u;
	u8 liberties = 0u;
	while (top > 0u) {
		const u8 point = stack[--top];
		u8 nb[4];
		const u8 n = neighbors(point, nb);
		for (u8 i = 0u; i < n; ++i) {
			const u8 q = nb[i];
			if (pos.board[q] == kEmpty) {
				++liberties;
			} else if (pos.board[q] == color && visited[q] == 0u) {
				visited[q] = 1u;
				stack[top++] = q;
			}
		}
	}
	return liberties;
}

/// ¿El grupo que contiene `start` tiene alguna libertad?
[[nodiscard]] inline bool group_alive(const Position& pos, u8 start, u8* visited) noexcept {
	return group_liberties(pos, start, visited) > 0u;
}

/// Información para deshacer: se guarda la posición completa (81+ bytes baratos y
/// evita reconstruir el conjunto de piedras capturadas).
struct Undo {
	Position previous;
};

} // namespace eng::board::go
