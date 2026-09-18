#pragma once

/// \file explain.hpp
/// **Explicador en lenguaje natural** de una posición de ajedrez, por plantillas y
/// reglas (template-based NLG). No usa ningún modelo de lenguaje: extrae los rasgos
/// de la posición (los mismos de la evaluación) y elige 2–4 frases preescritas en
/// español o inglés, con un tono neutro o enfático.
///
/// Flujo:
///
///   rasgos + evaluación  ->  reglas de prioridad  ->  plantillas  ->  párrafo
///
/// La interfaz es segura y polivalente: la salida es un `Span<char>` (con truncado
/// comprobado), las plantillas son `StringView` y los números se formatean con
/// `eng::util::to_chars_s32` (sin división). No hay punteros crudos ni `char*`.
///
/// Reglas actuales (por prioridad): jaque, ventaja material, dama prematura,
/// retraso de desarrollo y rey en el centro. Los conectores ("Además,",
/// "Por otro lado,") dan coherencia al párrafo.
///
/// Verificación: HOST-149.

#include <eng/board/core/types.hpp>
#include <eng/board/eval/chess_eval.hpp>
#include <eng/board/eval/features.hpp>
#include <eng/board/explain/templates.hpp>
#include <eng/board/rules/chess/board.hpp>
#include <eng/core/span.hpp>
#include <eng/core/util/static_string.hpp>
#include <eng/core/util/string_view.hpp>
#include <eng/core/util/text.hpp>

namespace eng::board::chess {

enum class Language : u8 {
	Spanish = 0u,
	English = 1u,
};

enum class Tone : u8 {
	Neutral = 0u,
	Emphatic = 1u,
};

struct ExplainOptions {
	Language language = Language::Spanish;
	Tone tone = Tone::Neutral;
	u32 max_phrases = 3u;
};

// --- Escritura en `Span<char>` con truncado seguro (nunca desborda) ---

inline void explain_put(eng::Span<char> out, eng::usize& n, char c) noexcept {
	if (n < out.size()) {
		out[n] = c;
	}
	++n;
}

inline void explain_put_text(eng::Span<char> out, eng::usize& n, eng::util::StringView text) noexcept {
	for (eng::usize i = 0u; i < text.size(); ++i) {
		explain_put(out, n, text[i]);
	}
}

/// Añade a `out` (desde `n`) la plantilla con sus huecos sustituidos.
inline void format_phrase(eng::util::StringView tmpl, eng::util::StringView side,
                          eng::util::StringView side_adj, board_int diff, eng::Span<char> out,
                          eng::usize& n) noexcept {
	eng::usize i = 0u;
	while (i < tmpl.size()) {
		const char c = tmpl[i];
		if (c != '{') {
			explain_put(out, n, c);
			++i;
			continue;
		}
		eng::usize end = i + 1u;
		while (end < tmpl.size() && tmpl[end] != '}') {
			++end;
		}
		const eng::util::StringView token = tmpl.substr(i + 1u, end - (i + 1u));
		if (token == eng::util::StringView {"side"}) {
			explain_put_text(out, n, side);
		} else if (token == eng::util::StringView {"side_adj"}) {
			explain_put_text(out, n, side_adj);
		} else if (token == eng::util::StringView {"diff"}) {
			eng::util::StaticString<16> digits;
			(void)eng::util::to_chars_u32(digits, static_cast<u32>(diff));
			explain_put_text(out, n, digits.view());
		}
		i = (end < tmpl.size()) ? (end + 1u) : tmpl.size();
	}
}

/// Divide por 100 sin `__divsi3` (resta repetida; el material es pequeño).
[[nodiscard]] inline board_int explain_div100(board_int value) noexcept {
	board_int rest = (value < 0) ? static_cast<board_int>(-value) : value;
	board_int quotient = 0;
	while (rest >= 100) {
		rest = static_cast<board_int>(rest - 100);
		++quotient;
	}
	return (value < 0) ? static_cast<board_int>(-quotient) : quotient;
}

namespace detail {

[[nodiscard]] inline eng::util::StringView side_name(Language language, board_int side) noexcept {
	if (language == Language::Spanish) {
		return eng::util::StringView {(side == 0) ? "blancas" : "negras"};
	}
	return eng::util::StringView {(side == 0) ? "White" : "Black"};
}

[[nodiscard]] inline eng::util::StringView side_adjective(Language language,
                                                          board_int side) noexcept {
	if (language == Language::Spanish) {
		return eng::util::StringView {(side == 0) ? "blanco" : "negro"};
	}
	return eng::util::StringView {(side == 0) ? "white" : "black"};
}

[[nodiscard]] inline eng::util::StringView connector(const Phrase& phrase,
                                                     Language language) noexcept {
	return (language == Language::Spanish) ? phrase.es : phrase.en;
}

[[nodiscard]] inline eng::util::StringView text_of(const Phrase& phrase,
                                                   Language language) noexcept {
	return (language == Language::Spanish) ? phrase.es : phrase.en;
}

} // namespace detail

/// Genera la explicación de `pos` en `out` (truncada con seguridad). Devuelve la
/// longitud lógica escrita.
[[nodiscard]] inline eng::usize explain(const Position& pos, ExplainOptions options,
                                        eng::Span<char> out) noexcept {
	const DevelopmentFeatures features = extract_development(pos);
	const EvalBreakdown eval = evaluate_white(pos);
	const board_int to_move_index = static_cast<board_int>(to_move(pos));
	const bool to_move_in_check = in_check(pos, to_move(pos));
	const bool emphatic = options.tone == Tone::Emphatic;

	eng::usize n = 0u;
	u32 emitted = 0u;
	const u32 limit = (options.max_phrases == 0u) ? 1u : options.max_phrases;

	auto emit = [&](const Phrase& phrase, board_int side_index, board_int diff) {
		if (emitted >= limit) {
			return;
		}
		if (emitted == 1u) {
			explain_put_text(out, n, detail::connector(kConnectorSecond, options.language));
		} else if (emitted == 2u) {
			explain_put_text(out, n, detail::connector(kConnectorThird, options.language));
		} else if (emitted >= 3u) {
			explain_put_text(out, n, eng::util::StringView {" "});
		}
		format_phrase(detail::text_of(phrase, options.language),
		              detail::side_name(options.language, side_index),
		              detail::side_adjective(options.language, side_index), diff, out, n);
		++emitted;
	};

	// Prioridad 1: jaque inmediato.
	if (to_move_in_check) {
		emit(emphatic ? kPhraseInCheckEmphatic : kPhraseInCheck, to_move_index, 0);
	}

	// Prioridad 2: ventaja material.
	const board_int material_pawns = explain_div100(static_cast<board_int>(eval.material));
	if (material_pawns != 0) {
		const board_int lead = (material_pawns > 0) ? 0 : 1;
		const board_int magnitude =
		    (material_pawns > 0) ? material_pawns : static_cast<board_int>(-material_pawns);
		emit((emphatic || magnitude >= 5) ? kPhraseMaterialEmphatic : kPhraseMaterial, lead,
		     magnitude);
	}

	// Prioridad 3: dama prematura.
	for (board_int side = 0; side < 2; ++side) {
		if (features.queen_moved_early[side]) {
			emit(kPhraseQueenEarly, side, 0);
		}
	}

	// Prioridad 4: desarrollo.
	for (board_int side = 0; side < 2; ++side) {
		if (features.undeveloped_minors[side] >= 3u && features.phase == GamePhase::Opening) {
			emit(kPhraseUndeveloped, side, 0);
		}
	}

	// Prioridad 5: rey en el centro.
	for (board_int side = 0; side < 2; ++side) {
		if (features.king_in_center[side]) {
			emit(kPhraseKingInCenter, side, 0);
		}
	}

	if (!out.empty()) {
		const eng::usize nul_at = (n < out.size()) ? n : (out.size() - 1u);
		out[nul_at] = '\0';
	}
	return n;
}

} // namespace eng::board::chess
