#pragma once

/// \file movegen.hpp
/// Generación legal y aplicación de jugadas de Go 9×9: coloca la piedra, retira los
/// grupos rivales sin libertades, prohíbe el suicidio y aplica **ko simple**. La
/// jugada resultante lleva el flag de captura (`go_move(point, captures)`) para que
/// la ordenación y la quiescence no tengan que simular.
///
/// Verificación: HOST-179.

#include <eng/board/core/types.hpp>
#include <eng/board/rules/go/board.hpp>

namespace eng::board::go {

/// Resultado de colocar una piedra.
struct Placement {
	bool suicide = false;
	bool superko = false;
	u8 removed_count = 0u;
	u8 removed_point = kNoPoint;
	u8 ko_point = kNoPoint;
};

/// Recoge el grupo de `start` en `group` y cuenta sus libertades. Marca `seen`.
[[nodiscard]] inline u8 collect_group(const Position& pos, u8 start, u8* group, u8* seen,
                                      u8& liberties) noexcept {
	const u8 color = pos.board[start];
	u8 stack[kCells];
	u8 top = 0u;
	stack[top++] = start;
	seen[start] = 1u;
	u8 size = 0u;
	u8 libs = 0u;
	while (top > 0u) {
		const u8 point = stack[--top];
		group[size++] = point;
		u8 nb[4];
		const u8 n = neighbors(point, nb);
		for (u8 i = 0u; i < n; ++i) {
			const u8 q = nb[i];
			if (pos.board[q] == kEmpty) {
				++libs;
			} else if (pos.board[q] == color && seen[q] == 0u) {
				seen[q] = 1u;
				stack[top++] = q;
			}
		}
	}
	liberties = libs;
	return size;
}

/// Aplica la colocación de una piedra en `point` produciendo `next`.
[[nodiscard]] inline Placement place(const Position& pos, u8 point, Position& next) noexcept {
	next = pos;
	const u8 me = pos.to_move;
	const u8 opp = opposite_stone(me);
	next.board[point] = me;

	u8 seen[kCells] {};
	u8 group[kCells];
	u8 removed_total = 0u;
	u8 removed_point = kNoPoint;

	u8 nb[4];
	const u8 n = neighbors(point, nb);
	for (u8 i = 0u; i < n; ++i) {
		const u8 q = nb[i];
		if (next.board[q] != opp || seen[q] != 0u) {
			continue;
		}
		u8 libs = 0u;
		const u8 size = collect_group(next, q, group, seen, libs);
		if (libs == 0u) {
			for (u8 k = 0u; k < size; ++k) {
				if (next.board[group[k]] == opp) {
					next.board[group[k]] = kEmpty;
					++removed_total;
					removed_point = group[k];
				}
			}
		}
	}

	u8 own_seen[kCells] {};
	u8 own_group[kCells];
	u8 own_libs = 0u;
	const u8 own_size = collect_group(next, point, own_group, own_seen, own_libs);

	Placement out {};
	out.suicide = (own_libs == 0u);
	out.removed_count = removed_total;
	out.removed_point = removed_point;
	if (!out.suicide && removed_total == 1u && own_size == 1u && own_libs == 1u) {
		out.ko_point = removed_point; // ko simple
	}
	next.captures[me] = static_cast<u8>(next.captures[me] + removed_total);
	next.ko_point = out.ko_point;
	next.passes = 0u; // una jugada de tablero reinicia los pases
	next.to_move = opp;
	next.key = compute_key(next);
	out.superko = position_repeated(pos, next.key);
	push_history(next, pos.key);
	return out;
}

/// Genera las jugadas legales (excluye suicidio, ko simple y superko) más el pase.
inline u32 generate_legal(const Position& pos, eng::board::MoveList& out) {
	out.clear();
	Position next;
	for (u8 point = 0u; point < kCells; ++point) {
		if (pos.board[point] != kEmpty || point == pos.ko_point) {
			continue;
		}
		const Placement pl = place(pos, point, next);
		if (pl.suicide || pl.superko) {
			continue;
		}
		out.push_back(go_move(point, pl.removed_count > 0u));
	}
	out.push_back(kGoPass); // pasar siempre es legal
	return static_cast<u32>(out.size());
}

/// Aplica una jugada legal actualizando `pos`; `undo` guarda la posición previa.
inline void make_move(Position& pos, Move move, Undo& undo) noexcept {
	undo.previous = pos;
	if (go_is_pass(move)) {
		Position next = pos;
		++next.passes;
		next.ko_point = kNoPoint;
		next.to_move = opposite_stone(next.to_move);
		next.key = compute_key(next);
		push_history(next, pos.key);
		pos = next;
		return;
	}
	Position next;
	(void)place(pos, go_point(move), next);
	pos = next;
}

/// Deshace una jugada restaurando la posición previa.
inline void unmake_move(Position& pos, Move, const Undo& undo) noexcept { pos = undo.previous; }

} // namespace eng::board::go
