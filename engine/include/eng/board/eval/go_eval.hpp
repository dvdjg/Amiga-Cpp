#pragma once

/// \file go_eval.hpp
/// Evaluación ligera de Go 9×9: **territorio** (regiones vacías rodeadas por un solo
/// color, por *flood fill*), **capturas** y penalización de grupos en **atari** (una
/// libertad). Devuelve la puntuación desde el bando al turno (negamax) y trabaja con
/// `board_int` (cabe de sobra en 16 bits).
///
/// Es deliberadamente simple: no hay ojos, influencia a distancia ni patrones todavía
/// (líneas futuras de B7); sirve para que la búsqueda tenga una señal estable.
///
/// Verificación: HOST-153.

#include <eng/board/core/types.hpp>
#include <eng/board/rules/go/board.hpp>
#include <eng/board/rules/go/movegen.hpp>

namespace eng::board::go {

/// Evalúa desde la perspectiva de las negras (positivo = mejor para negras).
[[nodiscard]] inline board_int evaluate_black(const Position& pos) noexcept {
	bool visited[kCells] {};
	board_int black_territory = 0;
	board_int white_territory = 0;

	// Territorio: cada región vacía tocada por un solo color cuenta para ese color.
	for (u8 start = 0u; start < kCells; ++start) {
		if (pos.board[start] != kEmpty || visited[start] != 0u) {
			continue;
		}
		u8 stack[kCells];
		u8 top = 0u;
		stack[top++] = start;
		visited[start] = 1u;
		board_int size = 0;
		bool touch_black = false;
		bool touch_white = false;
		while (top > 0u) {
			const u8 point = stack[--top];
			++size;
			u8 nb[4];
			const u8 n = neighbors(point, nb);
			for (u8 i = 0u; i < n; ++i) {
				const u8 q = nb[i];
				if (pos.board[q] == kEmpty) {
					if (visited[q] == 0u) {
						visited[q] = 1u;
						stack[top++] = q;
					}
				} else if (pos.board[q] == kBlack) {
					touch_black = true;
				} else if (pos.board[q] == kWhite) {
					touch_white = true;
				}
			}
		}
		if (touch_black && !touch_white) {
			black_territory += size;
		} else if (touch_white && !touch_black) {
			white_territory += size;
		}
	}

	// Atari: grupos con una única libertad se penalizan (pueden morir).
	u8 seen[kCells] {};
	board_int black_atari = 0;
	board_int white_atari = 0;
	for (u8 start = 0u; start < kCells; ++start) {
		if (pos.board[start] == kEmpty || seen[start] != 0u) {
			continue;
		}
		u8 group[kCells];
		u8 libs = 0u;
		const u8 size = collect_group(pos, start, group, seen, libs);
		if (libs == 1u) {
			if (pos.board[start] == kBlack) {
				black_atari += size;
			} else {
				white_atari += size;
			}
		}
	}

	return (black_territory - white_territory) +
	       (static_cast<board_int>(pos.captures[kBlack]) -
	        static_cast<board_int>(pos.captures[kWhite])) -
	       (black_atari << 1) + (white_atari << 1);
}

/// Evaluación desde el bando al turno (negamax).
[[nodiscard]] inline Score evaluate(const Position& pos) noexcept {
	const board_int black = evaluate_black(pos);
	return (pos.to_move == kBlack) ? static_cast<Score>(black) : static_cast<Score>(-black);
}

/// Policy de evaluación para el buscador genérico.
struct GoEval {
	[[nodiscard]] static Score evaluate(const Position& pos) noexcept { return go::evaluate(pos); }
};

} // namespace eng::board::go
