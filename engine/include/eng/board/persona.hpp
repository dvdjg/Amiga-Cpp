#pragma once

/// \file persona.hpp
/// **Puente entre `eng::board` y la capa de persona** (`eng::sim`): traduce los hechos
/// objetivos del motor de tablero (búsqueda, libro, evaluación, reloj) a `DecisionFacts` de
/// `eng::sim::introspection.hpp`, para que ajedrez y Go puedan expresar confianza, duda,
/// sorpresa, presión o satisfacción **gestualmente**.
///
/// - `facts_from_search`: a partir de un `Result` + el análisis Multi-PV (mejor y segunda
///   jugada), el libro y el tiempo, produce los hechos de una decisión.
/// - `facts_after_opponent`: los hechos tras ver la jugada del rival (para sorpresa/alerta).
///
/// El motor de tablero no conoce `eng::sim`: el juego hace el puente.
///
/// Verificación: HOST-206.

#include <eng/board/core/types.hpp>
#include <eng/board/search/search.hpp>
#include <eng/core/types.hpp>

#include <eng/sim/introspection.hpp>

namespace eng::board {

/// Hechos de una decisión del motor de tablero. `pv` son las mejores líneas del análisis
/// (Multi-PV); se usan la mejor y la segunda para medir el margen. `book_hit` indica si la
/// jugada salió del libro. `time_left` (0..255) es el tiempo restante normalizado.
[[nodiscard]] inline eng::sim::DecisionFacts facts_from_search(Score best_score,
                                                               eng::Span<const Score> line_scores,
                                                               u8 moves_available,
                                                               bool book_hit,
                                                               u8 time_left) noexcept {
	eng::sim::DecisionFacts f {};
	f.best_score = static_cast<eng::s32>(best_score);
	f.second_score = static_cast<eng::s32>(best_score);
	if (line_scores.size() >= 2u) {
		// La segunda mejor es la mayor de las restantes (las líneas vienen ordenadas).
		f.second_score = static_cast<eng::s32>(line_scores[1]);
	}
	f.moves_available = moves_available < 1u ? 1u : moves_available;
	f.book_hit = book_hit;
	f.time_left = time_left;
	return f;
}

/// Hechos tras ver la jugada del rival: `expected_score` era la evaluación que el motor
/// esperaba para sí mismo antes de que el rival moviera; `actual_score` es la de ahora.
/// El salto favorable delata un error del rival; el desfavorable, una sorpresa.
[[nodiscard]] inline eng::sim::DecisionFacts facts_after_opponent(Score expected_score,
                                                                  Score actual_score,
                                                                  u8 time_left) noexcept {
	eng::sim::DecisionFacts f {};
	f.best_score = static_cast<eng::s32>(actual_score);
	f.second_score = static_cast<eng::s32>(actual_score);
	f.prev_eval = static_cast<eng::s32>(expected_score);
	f.moves_available = 1u;
	f.time_left = time_left;
	return f;
}

} // namespace eng::board
