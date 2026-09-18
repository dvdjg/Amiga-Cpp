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
/// Reglas actuales (por prioridad): jaque, ventaja material, dama prematura,
/// retraso de desarrollo y rey en el centro. Los conectores ("Además,",
/// "Por otro lado,") dan coherencia al párrafo. El coste es de pocos kB de lógica
/// más las plantillas (`templates.hpp`).
///
/// Verificación: HOST-149.

#include <eng/board/core/types.hpp>
#include <eng/board/eval/chess_eval.hpp>
#include <eng/board/eval/features.hpp>
#include <eng/board/explain/templates.hpp>
#include <eng/board/rules/chess/board.hpp>

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

// --- Escritura con truncado seguro (nunca desborda `out`) ---

inline void explain_put(char* out, eng::usize cap, eng::usize& n, char c) noexcept {
	if (n + 1u < cap) {
		out[n] = c;
	}
	++n;
}

inline void explain_put_str(char* out, eng::usize cap, eng::usize& n, const char* text) noexcept {
	for (const char* p = text; *p != '\0'; ++p) {
		explain_put(out, cap, n, *p);
	}
}

inline void explain_put_int(char* out, eng::usize cap, eng::usize& n, int value) noexcept {
	if (value < 0) {
		explain_put(out, cap, n, '-');
		value = -value;
	}
	// Sin divisiones: `% 10`/`/ 10` acabarían en `__divsi3` (libcall de libgcc) en
	// 68000. Se extraen los dígitos restando potencias de diez.
	static constexpr eng::u32 powers[10] = {1u,       10u,      100u,      1000u,     10000u,
	                                        100000u,  1000000u, 10000000u, 100000000u, 1000000000u};
	eng::u32 v = static_cast<eng::u32>(value);
	int p = 9;
	while (p > 0 && powers[p] > v) {
		--p;
	}
	for (; p >= 0; --p) {
		eng::u32 digit = 0u;
		while (v >= powers[p]) {
			v -= powers[p];
			++digit;
		}
		explain_put(out, cap, n, static_cast<char>('0' + digit));
	}
}

/// Divide por 100 sin `__divsi3` (resta repetida; el material es pequeño).
[[nodiscard]] inline int explain_div100(int value) noexcept {
	int rest = (value < 0) ? -value : value;
	int quotient = 0;
	while (rest >= 100) {
		rest -= 100;
		++quotient;
	}
	// Sin `sign * quotient`: una multiplicación por ±1 sería `__mulsi3` en 68000.
	return (value < 0) ? -quotient : quotient;
}

[[nodiscard]] inline bool token_is(const char* token, eng::usize length, const char* word) noexcept {
	eng::usize i = 0u;
	for (; i < length && word[i] != '\0'; ++i) {
		if (token[i] != word[i]) {
			return false;
		}
	}
	return i == length && word[i] == '\0';
}

/// Añade a `out` (desde `n`) la plantilla con sus huecos sustituidos.
inline void format_phrase(const char* tmpl, const char* side, const char* side_adj, int diff,
                          char* out, eng::usize cap, eng::usize& n) noexcept {
	for (const char* p = tmpl; *p != '\0'; ++p) {
		if (*p != '{') {
			explain_put(out, cap, n, *p);
			continue;
		}
		char token[12];
		eng::usize len = 0u;
		++p;
		while (*p != '\0' && *p != '}' && len < sizeof(token)) {
			token[len++] = *p;
			++p;
		}
		if (token_is(token, len, "side")) {
			explain_put_str(out, cap, n, side);
		} else if (token_is(token, len, "side_adj")) {
			explain_put_str(out, cap, n, side_adj);
		} else if (token_is(token, len, "diff")) {
			explain_put_int(out, cap, n, diff);
		}
		// un hueco desconocido se ignora
	}
}

namespace detail {

struct ExplainEntry {
	const Phrase* phrase;
	int side_index;
	int diff;
};

[[nodiscard]] inline const char* side_name(Language language, int side) noexcept {
	if (language == Language::Spanish) {
		return (side == 0) ? "blancas" : "negras";
	}
	return (side == 0) ? "White" : "Black";
}

[[nodiscard]] inline const char* side_adjective(Language language, int side) noexcept {
	if (language == Language::Spanish) {
		return (side == 0) ? "blanco" : "negro";
	}
	return (side == 0) ? "white" : "black";
}

[[nodiscard]] inline const char* connector(const Phrase& phrase, Language language) noexcept {
	return (language == Language::Spanish) ? phrase.es : phrase.en;
}

[[nodiscard]] inline const char* text_of(const Phrase& phrase, Language language) noexcept {
	return (language == Language::Spanish) ? phrase.es : phrase.en;
}

} // namespace detail

/// Genera la explicación de `pos` en `out` (truncada con seguridad). Devuelve la
/// longitud lógica escrita.
[[nodiscard]] inline eng::usize explain(const Position& pos, ExplainOptions options, char* out,
                                        eng::usize cap) noexcept {
	using detail::ExplainEntry;

	const DevelopmentFeatures features = extract_development(pos);
	const EvalBreakdown eval = evaluate_white(pos);
	const int to_move_index = static_cast<int>(to_move(pos));
	const bool to_move_in_check = in_check(pos, to_move(pos));

	ExplainEntry entries[6] {};
	u32 count = 0u;
	const u32 limit = (options.max_phrases == 0u) ? 1u : options.max_phrases;
	auto add = [&](const Phrase& phrase, int side_index, int diff) {
		if (count < limit && count < 6u) {
			entries[count++] = ExplainEntry {&phrase, side_index, diff};
		}
	};

	// Prioridad 1: jaque inmediato.
	const bool emphatic = options.tone == Tone::Emphatic;
	if (to_move_in_check) {
		add(emphatic ? kPhraseInCheckEmphatic : kPhraseInCheck, to_move_index, 0);
	}

	// Prioridad 2: ventaja material.
	const int material_pawns = explain_div100(static_cast<int>(eval.material));
	if (material_pawns != 0) {
		const int lead = (material_pawns > 0) ? 0 : 1;
		const int magnitude = (material_pawns > 0) ? material_pawns : -material_pawns;
		add((emphatic || magnitude >= 5) ? kPhraseMaterialEmphatic : kPhraseMaterial, lead,
		    magnitude);
	}

	// Prioridad 3: dama prematura.
	for (int side = 0; side < 2; ++side) {
		if (features.queen_moved_early[side]) {
			add(kPhraseQueenEarly, side, 0);
		}
	}

	// Prioridad 4: desarrollo.
	for (int side = 0; side < 2; ++side) {
		if (features.undeveloped_minors[side] >= 3u && features.phase == GamePhase::Opening) {
			add(kPhraseUndeveloped, side, 0);
		}
	}

	// Prioridad 5: rey en el centro.
	for (int side = 0; side < 2; ++side) {
		if (features.king_in_center[side]) {
			add(kPhraseKingInCenter, side, 0);
		}
	}

	eng::usize n = 0u;
	for (u32 i = 0u; i < count; ++i) {
		if (i == 1u) {
			explain_put_str(out, cap, n, detail::connector(kConnectorSecond, options.language));
		} else if (i == 2u) {
			explain_put_str(out, cap, n, detail::connector(kConnectorThird, options.language));
		} else if (i >= 3u) {
			explain_put_str(out, cap, n, " ");
		}
		const ExplainEntry& entry = entries[i];
		const char* side = detail::side_name(options.language, entry.side_index);
		const char* side_adj = detail::side_adjective(options.language, entry.side_index);
		format_phrase(detail::text_of(*entry.phrase, options.language), side, side_adj, entry.diff,
		              out, cap, n);
	}
	out[n < cap ? n : (cap == 0u ? 0u : cap - 1u)] = '\0';
	return n;
}

} // namespace eng::board::chess
