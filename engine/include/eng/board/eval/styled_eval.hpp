#pragma once

/// \file styled_eval.hpp
/// Adaptador de evaluación por **estilo** para el buscador genérico.
///
/// El buscador fija su política de evaluación como tipo estático, así que el estilo
/// (pesos activos) vive en una variable global de dominio que el anfitrión fija justo
/// antes de buscar. En una máquina de un solo núcleo como el Amiga es suficiente; en
/// host hay que usarlo de forma secuencial.
///
/// Lo comparten la demo `demos/amiga/123_chess_match` y la simulación host
/// `tools/board/selfplay.cpp`, de modo que ambas usan **exactamente la misma**
/// evaluación por estilo.
///
/// Verificación: demo `123_chess_match` (build → run → analyze) y HOST-160.

#include <eng/board/eval/chess_eval.hpp>

namespace eng::board::chess {

/// Pesos activos; el llamador los fija antes de la búsqueda (`aggressive_weights()`,
/// `positional_weights()`, ...).
inline EvalWeights g_active_weights = positional_weights();

struct StyledEval {
	[[nodiscard]] static Score evaluate(const Position& pos) noexcept {
		return evaluate_styled(pos, g_active_weights);
	}
};

} // namespace eng::board::chess
